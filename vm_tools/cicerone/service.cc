// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/service.h"

#include <arpa/inet.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread_task_runner_handle.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_proxy.h>

#include "vm_tools/common/constants.h"

using std::string;

namespace vm_tools {
namespace cicerone {

namespace {

// Default name for a virtual machine.
constexpr char kDefaultVmName[] = "termina";

// Default name to use for a container.
constexpr char kDefaultContainerName[] = "penguin";

// Hostname for the default VM/container.
constexpr char kDefaultContainerHostname[] = "penguin.linux.test";

// Delimiter for the end of a URL scheme.
constexpr char kUrlSchemeDelimiter[] = "://";

// Hostnames we replace with the container IP if they are sent over in URLs to
// be opened by the host.
const char* const kLocalhostReplaceNames[] = {"localhost", "127.0.0.1"};

// Directory for runtime files.
constexpr char kRuntimeDir[] = "/run/vm_cicerone";

// SSH port for containers.
constexpr char kContainerSshPort[] = "2222";

// SSH binary name.
constexpr char kSshBin[] = "/usr/bin/ssh";

// SSH identity file name.
constexpr char kSshIdentityFilename[] = "private_key";

// SSH known_hosts file name.
constexpr char kSshKnownHostsFilename[] = "known_hosts";

// TCP ports to statically forward to the container over SSH.
const uint16_t kStaticForwardPorts[] = {
    3000,  // Rails
    4200,  // Angular
    5000,  // Flask
    8000,  // Django
    8008,  // HTTP alternative port
    8080,  // HTTP alternative port
    8888,  // ipython/jupyter
    9005,  // Firebase login
};

// Passes |method_call| to |handler| and passes the response to
// |response_sender|. If |handler| returns NULL, an empty response is created
// and sent.
void HandleSynchronousDBusMethodCall(
    base::Callback<std::unique_ptr<dbus::Response>(dbus::MethodCall*)> handler,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  std::unique_ptr<dbus::Response> response = handler.Run(method_call);
  if (!response)
    response = dbus::Response::FromMethodCall(method_call);
  response_sender.Run(std::move(response));
}

// Posted to a grpc thread to startup a listener service. Puts a copy of
// the pointer to the grpc server in |server_copy| and then signals |event|.
// It will listen on the address specified in |listener_address|.
void RunListenerService(grpc::Service* listener,
                        const std::string& vsock_listener_address,
                        const std::string& ipv4_listener_address,
                        base::WaitableEvent* event,
                        std::shared_ptr<grpc::Server>* server_copy) {
  // We are not interested in getting SIGCHLD or SIGTERM on this thread.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGTERM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // Build the grpc server.
  grpc::ServerBuilder builder;
  builder.AddListeningPort(vsock_listener_address,
                           grpc::InsecureServerCredentials());
  if (!ipv4_listener_address.empty()) {
    builder.AddListeningPort(ipv4_listener_address,
                             grpc::InsecureServerCredentials());
  }
  builder.RegisterService(listener);

  std::shared_ptr<grpc::Server> server(builder.BuildAndStart().release());

  *server_copy = server;
  event->Signal();

  if (server) {
    server->Wait();
  }
}

// Sets up a gRPC listener service by starting the |grpc_thread| and posting the
// main task to run for the thread. |listener_address| should be the address the
// gRPC server is listening on. A copy of the pointer to the server is put in
// |server_copy|. Returns true if setup & started successfully, false otherwise.
bool SetupListenerService(base::Thread* grpc_thread,
                          grpc::Service* listener_impl,
                          const std::string& vsock_listener_address,
                          const std::string& ipv4_listener_address,
                          std::shared_ptr<grpc::Server>* server_copy) {
  // Start the grpc thread.
  if (!grpc_thread->Start()) {
    LOG(ERROR) << "Failed to start grpc thread";
    return false;
  }

  base::WaitableEvent event(false /*manual_reset*/,
                            false /*initially_signaled*/);
  bool ret = grpc_thread->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&RunListenerService, listener_impl, vsock_listener_address,
                 ipv4_listener_address, &event, server_copy));
  if (!ret) {
    LOG(ERROR) << "Failed to post server startup task to grpc thread";
    return false;
  }

  // Wait for the VM grpc server to start.
  event.Wait();

  if (!server_copy) {
    LOG(ERROR) << "grpc server failed to start";
    return false;
  }

  return true;
}

// Converts an IPv4 address to a string. The result will be stored in |str|
// on success.
bool IPv4AddressToString(const uint32_t address, std::string* str) {
  CHECK(str);

  char result[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &address, result, sizeof(result)) != result) {
    return false;
  }
  *str = std::string(result);
  return true;
}

// Replaces either localhost or 127.0.0.1 in the hostname part of a URL with the
// IP address of the container itself.
std::string ReplaceLocalhostInUrl(const std::string& url,
                                  const std::string& alt_host) {
  // We don't have any URL parsing libraries at our disposal here without
  // integrating something new, so just do some basic URL parsing ourselves.
  // First find where the scheme ends, which'll be after the first :// string.
  // Then search for the next / char, which will start the path for the URL, the
  // hostname will be in the string between those two.
  // Also check for an @ symbol, which may have a user/pass before the hostname
  // and then check for a : at the end for an optional port.
  // scheme://[user:pass@]hostname[:port]/path
  auto front = url.find(kUrlSchemeDelimiter);
  if (front == std::string::npos) {
    return url;
  }
  front += sizeof(kUrlSchemeDelimiter) - 1;
  auto back = url.find('/', front);
  if (back == std::string::npos) {
    // This isn't invalid, such as http://google.com.
    back = url.length();
  }
  auto at_check = url.find('@', front);
  if (at_check != std::string::npos && at_check < back) {
    front = at_check + 1;
  }
  auto port_check = url.find(':', front);
  if (port_check != std::string::npos && port_check < back) {
    back = port_check;
  }
  // We don't care about URL validity, but our logic should ensure that front
  // is less than back at this point and this checks that.
  CHECK_LE(front, back);
  std::string hostname = url.substr(front, back - front);
  for (const auto host_check : kLocalhostReplaceNames) {
    if (hostname == host_check) {
      // Replace the hostname with the alternate hostname which will be the
      // container's IP address.
      return url.substr(0, front) + alt_host + url.substr(back);
    }
  }
  return url;
}

}  // namespace

std::unique_ptr<Service> Service::Create(base::Closure quit_closure) {
  auto service = base::WrapUnique(new Service(std::move(quit_closure)));

  if (!service->Init()) {
    service.reset();
  }

  return service;
}

Service::Service(base::Closure quit_closure)
    : watcher_(FROM_HERE),
      quit_closure_(std::move(quit_closure)),
      weak_ptr_factory_(this) {
  container_listener_ =
      std::make_unique<ContainerListenerImpl>(weak_ptr_factory_.GetWeakPtr());
  tremplin_listener_ =
      std::make_unique<TremplinListenerImpl>(weak_ptr_factory_.GetWeakPtr());
}

Service::~Service() {
  if (grpc_server_container_) {
    grpc_server_container_->Shutdown();
  }
}

void Service::OnFileCanReadWithoutBlocking(int fd) {
  DCHECK_EQ(signal_fd_.get(), fd);

  struct signalfd_siginfo siginfo;
  if (read(signal_fd_.get(), &siginfo, sizeof(siginfo)) != sizeof(siginfo)) {
    PLOG(ERROR) << "Failed to read from signalfd";
    return;
  }

  if (siginfo.ssi_signo == SIGCHLD) {
    HandleChildExit();
  } else if (siginfo.ssi_signo == SIGTERM) {
    HandleSigterm();
  } else {
    LOG(ERROR) << "Received unknown signal from signal fd: "
               << strsignal(siginfo.ssi_signo);
  }
}

void Service::OnFileCanWriteWithoutBlocking(int fd) {
  NOTREACHED();
}

void Service::ConnectTremplin(uint32_t cid,
                              bool* result,
                              base::WaitableEvent* event) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(result);
  CHECK(event);
  *result = false;
  VirtualMachine* vm;
  std::string vm_name;
  std::string owner_id;
  if (!GetVirtualMachineForCid(cid, &vm, &owner_id, &vm_name)) {
    event->Signal();
    return;
  }

  // Found the VM with a matching VM IP, so connect to the tremplin instance.
  if (!vm->ConnectTremplin()) {
    LOG(ERROR) << "Failed to connect to tremplin";
    event->Signal();
    return;
  }

  // Send the D-Bus signal out to indicate tremplin is ready.
  dbus::Signal signal(kVmCiceroneInterface, kTremplinStartedSignal);
  vm_tools::cicerone::TremplinStartedSignal proto;
  proto.set_vm_name(vm_name);
  proto.set_owner_id(owner_id);
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
  exported_object_->SendSignal(&signal);
  *result = true;
  event->Signal();
}

void Service::LxdContainerCreated(const uint32_t cid,
                                  std::string container_name,
                                  Service::CreateStatus status,
                                  std::string failure_reason,
                                  bool* result,
                                  base::WaitableEvent* event) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(!container_name.empty());
  CHECK(result);
  CHECK(event);
  *result = false;
  VirtualMachine* vm;
  std::string vm_name;
  std::string owner_id;
  if (!GetVirtualMachineForCid(cid, &vm, &owner_id, &vm_name)) {
    event->Signal();
    return;
  }

  dbus::Signal signal(kVmCiceroneInterface, kLxdContainerCreatedSignal);
  vm_tools::cicerone::LxdContainerCreatedSignal proto;
  proto.mutable_vm_name()->swap(vm_name);
  proto.set_container_name(container_name);
  proto.mutable_owner_id()->swap(owner_id);
  proto.set_failure_reason(failure_reason);
  switch (status) {
    case Service::CreateStatus::CREATED:
      proto.set_status(LxdContainerCreatedSignal::CREATED);
      break;
    case Service::CreateStatus::DOWNLOAD_TIMED_OUT:
      proto.set_status(LxdContainerCreatedSignal::DOWNLOAD_TIMED_OUT);
      break;
    case Service::CreateStatus::CANCELLED:
      proto.set_status(LxdContainerCreatedSignal::CANCELLED);
      break;
    case Service::CreateStatus::FAILED:
      proto.set_status(LxdContainerCreatedSignal::FAILED);
      break;
    default:
      proto.set_status(LxdContainerCreatedSignal::UNKNOWN);
      break;
  }
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
  exported_object_->SendSignal(&signal);
  *result = true;
  event->Signal();
}

void Service::LxdContainerDownloading(const uint32_t cid,
                                      std::string container_name,
                                      int download_progress,
                                      bool* result,
                                      base::WaitableEvent* event) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(!container_name.empty());
  CHECK(result);
  CHECK(event);
  *result = false;
  VirtualMachine* vm;
  std::string vm_name;
  std::string owner_id;
  if (!GetVirtualMachineForCid(cid, &vm, &owner_id, &vm_name)) {
    event->Signal();
    return;
  }

  dbus::Signal signal(kVmCiceroneInterface, kLxdContainerDownloadingSignal);
  vm_tools::cicerone::LxdContainerDownloadingSignal proto;
  proto.set_vm_name(std::move(vm_name));
  proto.set_download_progress(std::move(download_progress));
  proto.set_owner_id(std::move(owner_id));
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
  exported_object_->SendSignal(&signal);
  *result = true;
  event->Signal();
}

void Service::ContainerStartupCompleted(const std::string& container_token,
                                        const uint32_t cid,
                                        const uint32_t garcon_vsock_port,
                                        uint32_t container_ip,
                                        bool* result,
                                        base::WaitableEvent* event) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(result);
  CHECK(event);
  *result = false;
  VirtualMachine* vm;
  std::string vm_name;
  std::string owner_id;
  if (!GetVirtualMachineForCid(cid, &vm, &owner_id, &vm_name) &&
      !GetVirtualMachineForContainerIp(container_ip, &vm, &owner_id,
                                       &vm_name)) {
    event->Signal();
    return;
  }

  Container* container = vm->GetPendingContainerForToken(container_token);
  if (!container) {
    // This could be a garcon restart.
    container = vm->GetContainerForToken(container_token);
    if (!container) {
      LOG(ERROR) << "Received ContainerStartupCompleted for unknown container";
      return;
    }
  }
  if (container_ip == 0) {
    VirtualMachine::LxdContainerInfo info;
    std::string error;
    VirtualMachine::GetLxdContainerInfoStatus status =
        vm->GetLxdContainerInfo(container->name(), &info, &error);
    if (status != VirtualMachine::GetLxdContainerInfoStatus::RUNNING) {
      LOG(ERROR) << "Failed to retreive IPv4 address for container: " << error;
      return;
    }
    container_ip = info.ipv4_address;
    container->set_ipv4_address(info.ipv4_address);
  } else {
    container->set_ipv4_address(container_ip);
  }

  // Found the VM with a matching container subnet, register the IP address
  // for the container with that VM object.
  std::string string_ip;
  if (!IPv4AddressToString(container_ip, &string_ip)) {
    LOG(ERROR) << "Failed converting IP address to string: " << container_ip;
    event->Signal();
    return;
  }
  if (!vm->RegisterContainer(container_token, garcon_vsock_port, string_ip)) {
    LOG(ERROR) << "Invalid container token passed back from VM " << vm_name
               << " of " << container_token;
    event->Signal();
    return;
  }
  std::string container_name = vm->GetContainerNameForToken(container_token);
  LOG(INFO) << "Startup of container " << container_name << " at IP "
            << string_ip << " for VM " << vm_name << " completed.";

  if (owner_id == primary_owner_id_) {
    // Register this with the hostname resolver.
    RegisterHostname(
        base::StringPrintf("%s.%s.linux.test", container_name.c_str(),
                           vm_name.c_str()),
        string_ip);
    if (vm_name == kDefaultVmName && container_name == kDefaultContainerName) {
      RegisterHostname(kDefaultContainerHostname, string_ip);

      std::string username, error_msg;
      if (vm->GetLxdContainerUsername(container_name, &username, &error_msg) !=
          VirtualMachine::GetLxdContainerUsernameStatus::SUCCESS) {
        LOG(ERROR) << "Failed to get container " << container_name
                   << " username for SSH forwarding: " << error_msg;
      } else {
        StartSshForwarding(owner_id, string_ip, username);
      }
    }
  }

  // Send the D-Bus signal out to indicate the container is ready.
  dbus::Signal signal(kVmCiceroneInterface, kContainerStartedSignal);
  vm_tools::cicerone::ContainerStartedSignal proto;
  proto.set_vm_name(vm_name);
  proto.set_container_name(container_name);
  proto.set_owner_id(owner_id);
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
  exported_object_->SendSignal(&signal);
  *result = true;
  event->Signal();
}

void Service::ContainerShutdown(const std::string& container_token,
                                const uint32_t cid,
                                const uint32_t container_ip,
                                bool* result,
                                base::WaitableEvent* event) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(result);
  CHECK(event);
  *result = false;
  VirtualMachine* vm;
  std::string owner_id;
  std::string vm_name;

  if (!GetVirtualMachineForCid(cid, &vm, &owner_id, &vm_name) &&
      !GetVirtualMachineForContainerIp(container_ip, &vm, &owner_id,
                                       &vm_name)) {
    event->Signal();
    return;
  }
  std::string container_name = vm->GetContainerNameForToken(container_token);
  if (!vm->UnregisterContainer(container_token)) {
    LOG(ERROR) << "Invalid container token passed back from VM " << vm_name
               << " of " << container_token;
    event->Signal();
    return;
  }
  // Unregister this with the hostname resolver.
  UnregisterHostname(base::StringPrintf(
      "%s.%s.linux.test", container_name.c_str(), vm_name.c_str()));
  if (vm_name == kDefaultVmName && container_name == kDefaultContainerName) {
    UnregisterHostname(kDefaultContainerHostname);
    ssh_process_.Reset(0);
  }

  LOG(INFO) << "Shutdown of container " << container_name << " for VM "
            << vm_name;

  // Send the D-Bus signal out to indicate the container has shutdown.
  dbus::Signal signal(kVmCiceroneInterface, kContainerShutdownSignal);
  ContainerShutdownSignal proto;
  proto.set_vm_name(vm_name);
  proto.set_container_name(container_name);
  proto.set_owner_id(owner_id);
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
  exported_object_->SendSignal(&signal);
  *result = true;
  event->Signal();
}

void Service::UpdateApplicationList(const std::string& container_token,
                                    const uint32_t cid,
                                    const uint32_t container_ip,
                                    vm_tools::apps::ApplicationList* app_list,
                                    bool* result,
                                    base::WaitableEvent* event) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(app_list);
  CHECK(result);
  CHECK(event);
  *result = false;
  std::string owner_id;
  std::string vm_name;
  VirtualMachine* vm;
  if (!GetVirtualMachineForCid(cid, &vm, &owner_id, &vm_name) &&
      !GetVirtualMachineForContainerIp(container_ip, &vm, &owner_id,
                                       &vm_name)) {
    event->Signal();
    return;
  }
  std::string container_name = vm->GetContainerNameForToken(container_token);
  if (container_name.empty()) {
    event->Signal();
    return;
  }
  app_list->set_vm_name(vm_name);
  app_list->set_container_name(container_name);
  app_list->set_owner_id(owner_id);
  dbus::MethodCall method_call(
      vm_tools::apps::kVmApplicationsServiceInterface,
      vm_tools::apps::kVmApplicationsServiceUpdateApplicationListMethod);
  dbus::MessageWriter writer(&method_call);

  if (!writer.AppendProtoAsArrayOfBytes(*app_list)) {
    LOG(ERROR) << "Failed to encode ApplicationList protobuf";
    event->Signal();
    return;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      vm_applications_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to crostini app registry";
  } else {
    *result = true;
  }
  event->Signal();
}

void Service::OpenUrl(const std::string& container_token,
                      const std::string& url,
                      uint32_t cid,
                      uint32_t container_ip,
                      bool* result,
                      base::WaitableEvent* event) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(result);
  CHECK(event);
  *result = false;
  dbus::MethodCall method_call(chromeos::kUrlHandlerServiceInterface,
                               chromeos::kUrlHandlerServiceOpenUrlMethod);
  dbus::MessageWriter writer(&method_call);

  std::string owner_id;
  std::string vm_name;
  VirtualMachine* vm;
  if (!GetVirtualMachineForCid(cid, &vm, &owner_id, &vm_name) &&
      !GetVirtualMachineForContainerIp(container_ip, &vm, &owner_id,
                                       &vm_name)) {
    event->Signal();
    return;
  }
  Container* container = vm->GetContainerForToken(container_token);
  if (!container) {
    LOG(ERROR) << "No container found matching token: " << container_token;
    event->Signal();
    return;
  }
  std::string container_ip_str;
  if (!IPv4AddressToString(container->ipv4_address(), &container_ip_str)) {
    LOG(ERROR) << "Failed converting IP address to string: " << container_ip;
    event->Signal();
    return;
  }
  if (container_ip_str == linuxhost_ip_) {
    container_ip_str = kDefaultContainerHostname;
  }
  writer.AppendString(ReplaceLocalhostInUrl(url, container_ip_str));
  std::unique_ptr<dbus::Response> dbus_response =
      url_handler_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to Chrome for OpenUrl";
  } else {
    *result = true;
  }
  event->Signal();
}

void Service::InstallLinuxPackageProgress(
    const std::string& container_token,
    const uint32_t cid,
    const uint32_t container_ip,
    InstallLinuxPackageProgressSignal* progress_signal,
    bool* result,
    base::WaitableEvent* event) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(progress_signal);
  CHECK(result);
  CHECK(event);
  *result = false;
  VirtualMachine* vm;
  std::string owner_id;
  std::string vm_name;

  if (!GetVirtualMachineForCid(cid, &vm, &owner_id, &vm_name) &&
      !GetVirtualMachineForContainerIp(container_ip, &vm, &owner_id,
                                       &vm_name)) {
    event->Signal();
    return;
  }
  std::string container_name = vm->GetContainerNameForToken(container_token);
  if (container_name.empty()) {
    event->Signal();
    return;
  }

  // Send the D-Bus signal out updating progress/completion for the install.
  dbus::Signal signal(kVmCiceroneInterface, kInstallLinuxPackageProgressSignal);
  progress_signal->set_vm_name(vm_name);
  progress_signal->set_container_name(container_name);
  progress_signal->set_owner_id(owner_id);
  dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(*progress_signal);
  exported_object_->SendSignal(&signal);
  *result = true;
  event->Signal();
}

bool Service::Init() {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(std::move(opts));

  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return false;
  }

  exported_object_ =
      bus_->GetExportedObject(dbus::ObjectPath(kVmCiceroneServicePath));
  if (!exported_object_) {
    LOG(ERROR) << "Failed to export " << kVmCiceroneServicePath << " object";
    return false;
  }

  using ServiceMethod =
      std::unique_ptr<dbus::Response> (Service::*)(dbus::MethodCall*);
  const std::map<const char*, ServiceMethod> kServiceMethods = {
      {kNotifyVmStartedMethod, &Service::NotifyVmStarted},
      {kNotifyVmStoppedMethod, &Service::NotifyVmStopped},
      {kGetContainerTokenMethod, &Service::GetContainerToken},
      {kIsContainerRunningMethod, &Service::IsContainerRunning},
      {kLaunchContainerApplicationMethod, &Service::LaunchContainerApplication},
      {kGetContainerAppIconMethod, &Service::GetContainerAppIcon},
      {kLaunchVshdMethod, &Service::LaunchVshd},
      {kGetLinuxPackageInfoMethod, &Service::GetLinuxPackageInfo},
      {kInstallLinuxPackageMethod, &Service::InstallLinuxPackage},
      {kCreateLxdContainerMethod, &Service::CreateLxdContainer},
      {kStartLxdContainerMethod, &Service::StartLxdContainer},
      {kGetLxdContainerUsernameMethod, &Service::GetLxdContainerUsername},
      {kSetUpLxdContainerUserMethod, &Service::SetUpLxdContainerUser},
      {kGetDebugInformation, &Service::GetDebugInformation},
  };

  for (const auto& iter : kServiceMethods) {
    bool ret = exported_object_->ExportMethodAndBlock(
        kVmCiceroneInterface, iter.first,
        base::Bind(&HandleSynchronousDBusMethodCall,
                   base::Bind(iter.second, base::Unretained(this))));
    if (!ret) {
      LOG(ERROR) << "Failed to export method " << iter.first;
      return false;
    }
  }

  if (!bus_->RequestOwnershipAndBlock(kVmCiceroneServiceName,
                                      dbus::Bus::REQUIRE_PRIMARY)) {
    LOG(ERROR) << "Failed to take ownership of " << kVmCiceroneServiceName;
    return false;
  }

  // Get the D-Bus proxy for communicating with the crostini registry in Chrome
  // and for the URL handler service.
  vm_applications_service_proxy_ = bus_->GetObjectProxy(
      vm_tools::apps::kVmApplicationsServiceName,
      dbus::ObjectPath(vm_tools::apps::kVmApplicationsServicePath));
  if (!vm_applications_service_proxy_) {
    LOG(ERROR) << "Unable to get dbus proxy for "
               << vm_tools::apps::kVmApplicationsServiceName;
    return false;
  }
  url_handler_service_proxy_ =
      bus_->GetObjectProxy(chromeos::kUrlHandlerServiceName,
                           dbus::ObjectPath(chromeos::kUrlHandlerServicePath));
  if (!url_handler_service_proxy_) {
    LOG(ERROR) << "Unable to get dbus proxy for "
               << chromeos::kUrlHandlerServiceName;
    return false;
  }
  crosdns_service_proxy_ =
      bus_->GetObjectProxy(crosdns::kCrosDnsServiceName,
                           dbus::ObjectPath(crosdns::kCrosDnsServicePath));
  if (!crosdns_service_proxy_) {
    LOG(ERROR) << "Unable to get dbus proxy for "
               << crosdns::kCrosDnsServiceName;
    return false;
  }
  crosdns_service_proxy_->WaitForServiceToBeAvailable(base::Bind(
      &Service::OnCrosDnsServiceAvailable, weak_ptr_factory_.GetWeakPtr()));

  concierge_service_proxy_ = bus_->GetObjectProxy(
      vm_tools::concierge::kVmConciergeServiceName,
      dbus::ObjectPath(vm_tools::concierge::kVmConciergeServicePath));
  if (!concierge_service_proxy_) {
    LOG(ERROR) << "Unable to get dbus proxy for "
               << vm_tools::concierge::kVmConciergeServiceName;
    return false;
  }

  // Setup & start the gRPC listener services.
  if (!SetupListenerService(
          &grpc_thread_container_, container_listener_.get(),
          base::StringPrintf("vsock:%u:%u", VMADDR_CID_ANY,
                             vm_tools::kGarconPort),
          base::StringPrintf("[::]:%u", vm_tools::kGarconPort),
          &grpc_server_container_)) {
    LOG(ERROR) << "Failed to setup/startup the container grpc server";
    return false;
  }

  if (!SetupListenerService(&grpc_thread_tremplin_, tremplin_listener_.get(),
                            base::StringPrintf("vsock:%u:%u", VMADDR_CID_ANY,
                                               vm_tools::kTremplinListenerPort),
                            "", &grpc_server_tremplin_)) {
    LOG(ERROR) << "Failed to setup/startup the tremplin grpc server";
    return false;
  }
  LOG(INFO) << "Started tremplin grpc server";

  // Set up the signalfd for receiving SIGCHLD and SIGTERM.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGTERM);

  signal_fd_.reset(signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
  if (!signal_fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create signalfd";
    return false;
  }

  bool ret = base::MessageLoopForIO::current()->WatchFileDescriptor(
      signal_fd_.get(), true /*persistent*/, base::MessageLoopForIO::WATCH_READ,
      &watcher_, this);
  if (!ret) {
    LOG(ERROR) << "Failed to watch signalfd";
    return false;
  }

  // Now block signals from the normal signal handling path so that we will get
  // them via the signalfd.
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
    PLOG(ERROR) << "Failed to block signals via sigprocmask";
    return false;
  }

  return true;
}

void Service::HandleChildExit() {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  // We can't just rely on the information in the siginfo structure because
  // more than one child may have exited but only one SIGCHLD will be
  // generated.
  while (true) {
    int status;
    pid_t pid = waitpid(-1, &status, WNOHANG);
    if (pid <= 0) {
      if (pid == -1 && errno != ECHILD) {
        PLOG(ERROR) << "Unable to reap child processes";
      }
      break;
    }

    if (WIFEXITED(status)) {
      LOG(INFO) << " Process " << pid << " exited with status "
                << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      LOG(INFO) << " Process " << pid << " killed by signal "
                << WTERMSIG(status)
                << (WCOREDUMP(status) ? " (core dumped)" : "");
    } else {
      LOG(WARNING) << "Unknown exit status " << status << " for process "
                   << pid;
    }

    ssh_process_.Release();
    ssh_process_.Reset(0);
  }
}
void Service::HandleSigterm() {
  LOG(INFO) << "Shutting down due to SIGTERM";

  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, quit_closure_);
}

std::unique_ptr<dbus::Response> Service::NotifyVmStarted(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received NotifyVmStarted request";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  NotifyVmStartedRequest request;
  EmptyMessage response;
  writer.AppendProtoAsArrayOfBytes(response);

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse NotifyVmStartedRequest from message";
    return dbus_response;
  }

  vms_[std::make_pair(request.owner_id(), std::move(request.vm_name()))] =
      std::make_unique<VirtualMachine>(request.container_ipv4_subnet(),
                                       request.container_ipv4_netmask(),
                                       request.ipv4_address(),
                                       request.cid());
  if (primary_owner_id_.empty() || vms_.empty()) {
    primary_owner_id_ = request.owner_id();
  }
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::NotifyVmStopped(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received NotifyVmStopped request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  NotifyVmStoppedRequest request;
  EmptyMessage response;
  writer.AppendProtoAsArrayOfBytes(response);

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse NotifyVmStoppedRequest from message";
    return dbus_response;
  }

  VmKey vm_key =
      std::make_pair(std::move(request.owner_id()), request.vm_name());
  auto iter = vms_.find(vm_key);
  if (iter == vms_.end()) {
    LOG(ERROR) << "Requested VM does not exist: " << request.vm_name();
    return dbus_response;
  }

  UnregisterVmContainers(iter->second.get(), iter->first.first,
                         iter->first.second);

  vms_.erase(iter);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::GetContainerToken(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received GetContainerToken request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ContainerTokenRequest request;
  ContainerTokenResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ContainerTokenRequest from message";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  response.set_container_token(
      vm->GenerateContainerToken(std::move(request.container_name())));
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::IsContainerRunning(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received IsContainerRunning request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  IsContainerRunningRequest request;
  IsContainerRunningResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse IsContainerRunningRequest from message";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  std::string container_name = request.container_name().empty()
                                   ? kDefaultContainerName
                                   : request.container_name();
  Container* container = vm->GetContainerForName(container_name);
  if (!container) {
    LOG(ERROR) << "Requested container does not exist: " << container_name;
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  response.set_container_running(container->IsRunning());
  writer.AppendProtoAsArrayOfBytes(response);

  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::LaunchContainerApplication(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received LaunchContainerApplication request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  LaunchContainerApplicationRequest request;
  LaunchContainerApplicationResponse response;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse LaunchContainerApplicationRequest from "
               << "message";
    response.set_success(false);
    response.set_failure_reason(
        "Unable to parse LaunchContainerApplicationRequest");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    response.set_success(false);
    response.set_failure_reason("Requested VM does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  std::string container_name = request.container_name().empty()
                                   ? kDefaultContainerName
                                   : request.container_name();
  Container* container = vm->GetContainerForName(container_name);
  if (!container) {
    LOG(ERROR) << "Requested container does not exist: " << container_name;
    response.set_success(false);
    response.set_failure_reason("Requested container does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.desktop_file_id().empty()) {
    LOG(ERROR) << "LaunchContainerApplicationRequest had an empty "
               << "desktop_file_id";
    response.set_success(false);
    response.set_failure_reason("Empty desktop_file_id in request");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string error_msg;
  response.set_success(container->LaunchContainerApplication(
      request.desktop_file_id(),
      std::vector<string>(
          std::make_move_iterator(request.mutable_files()->begin()),
          std::make_move_iterator(request.mutable_files()->end())),
      &error_msg));
  response.set_failure_reason(error_msg);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::GetContainerAppIcon(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received GetContainerAppIcon request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  ContainerAppIconRequest request;
  ContainerAppIconResponse response;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse ContainerAppIconRequest from message";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  std::string container_name = request.container_name().empty()
                                   ? kDefaultContainerName
                                   : request.container_name();
  Container* container = vm->GetContainerForName(container_name);
  if (!container) {
    LOG(ERROR) << "Requested container does not exist: " << container_name;
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.desktop_file_ids().size() == 0) {
    LOG(ERROR) << "ContainerAppIconRequest had an empty desktop_file_ids";
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::vector<std::string> desktop_file_ids;
  for (std::string& id : *request.mutable_desktop_file_ids()) {
    desktop_file_ids.emplace_back(std::move(id));
  }

  std::vector<Container::Icon> icons;
  icons.reserve(desktop_file_ids.size());

  if (!container->GetContainerAppIcon(std::move(desktop_file_ids),
                                      request.size(), request.scale(),
                                      &icons)) {
    LOG(ERROR) << "GetContainerAppIcon failed";
  }

  for (auto& container_icon : icons) {
    auto* icon = response.add_icons();
    *icon->mutable_desktop_file_id() =
        std::move(container_icon.desktop_file_id);
    *icon->mutable_icon() = std::move(container_icon.content);
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::LaunchVshd(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received LaunchVshd request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  LaunchVshdRequest request;
  LaunchVshdResponse response;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse LaunchVshdRequest from message";
    response.set_failure_reason(
        "unable to parse LaunchVshdRequest from message");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (request.port() == 0) {
    LOG(ERROR) << "Port is not set in LaunchVshdRequest";
    response.set_failure_reason("port is not set in LaunchVshdRequest");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // TODO(jkardatzke): Remove the empty string check once Chrome is updated
  // to put the owner_id in this request.
  std::string owner_id = request.owner_id().empty()
                             ? primary_owner_id_
                             : std::move(request.owner_id());
  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist: " << request.vm_name();
    response.set_failure_reason(base::StringPrintf(
        "requested VM does not exist: %s", request.vm_name().c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  std::string container_name = request.container_name().empty()
                                   ? kDefaultContainerName
                                   : request.container_name();
  Container* container = vm->GetContainerForName(container_name);
  if (!container) {
    LOG(ERROR) << "Requested container does not exist: " << container_name;
    response.set_failure_reason(base::StringPrintf(
        "requested container does not exist: %s", container_name.c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string error_msg;
  container->LaunchVshd(request.port(), &error_msg);

  response.set_success(true);
  response.set_failure_reason(error_msg);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::GetLinuxPackageInfo(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received GetLinuxPackageInfo request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  LinuxPackageInfoRequest request;
  LinuxPackageInfoResponse response;
  response.set_success(false);
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse LinuxPackageInfoRequest from message";
    response.set_failure_reason("Unable to parse request protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  if (request.file_path().empty()) {
    LOG(ERROR) << "Linux file path is not set in request";
    response.set_failure_reason("Linux file path is not set in request");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    response.set_failure_reason(base::StringPrintf(
        "requested VM does not exist: %s", request.vm_name().c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  std::string container_name = request.container_name().empty()
                                   ? kDefaultContainerName
                                   : request.container_name();
  Container* container = vm->GetContainerForName(container_name);
  if (!container) {
    LOG(ERROR) << "Requested container does not exist: " << container_name;
    response.set_failure_reason(base::StringPrintf(
        "requested container does not exist: %s", container_name.c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string error_msg;
  Container::LinuxPackageInfo pkg_info;
  response.set_success(container->GetLinuxPackageInfo(request.file_path(),
                                                      &pkg_info, &error_msg));
  if (response.success()) {
    response.set_package_id(pkg_info.package_id);
    response.set_license(pkg_info.license);
    response.set_description(pkg_info.description);
    response.set_project_url(pkg_info.project_url);
    response.set_size(pkg_info.size);
    response.set_summary(pkg_info.summary);
  } else {
    response.set_failure_reason(error_msg);
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::InstallLinuxPackage(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received InstallLinuxPackage request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  InstallLinuxPackageRequest request;
  InstallLinuxPackageResponse response;
  response.set_status(InstallLinuxPackageResponse::FAILED);
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse InstallLinuxPackageRequest from message";
    response.set_failure_reason("Unable to parse request protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  if (request.file_path().empty()) {
    LOG(ERROR) << "Linux file path is not set in request";
    response.set_failure_reason("Linux file path is not set in request");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    response.set_failure_reason("Requested VM does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  std::string container_name = request.container_name().empty()
                                   ? kDefaultContainerName
                                   : request.container_name();
  Container* container = vm->GetContainerForName(container_name);
  if (!container) {
    LOG(ERROR) << "Requested container does not exist: " << container_name;
    response.set_failure_reason(base::StringPrintf(
        "requested container does not exist: %s", container_name.c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string error_msg;
  int status = container->InstallLinuxPackage(request.file_path(), &error_msg);
  response.set_status(static_cast<InstallLinuxPackageResponse::Status>(status));
  response.set_failure_reason(error_msg);

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::CreateLxdContainer(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received CreateLxdContainer request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  CreateLxdContainerRequest request;
  CreateLxdContainerResponse response;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse CreateLxdRequest from message";
    response.set_failure_reason(
        "unable to parse CreateLxdRequest from message");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    response.set_failure_reason(base::StringPrintf(
        "requested VM does not exist: %s", request.vm_name().c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string error_msg;
  VirtualMachine::CreateLxdContainerStatus status = vm->CreateLxdContainer(
      request.container_name().empty() ? kDefaultContainerName
                                       : request.container_name(),
      request.image_server(), request.image_alias(), &error_msg);

  switch (status) {
    case VirtualMachine::CreateLxdContainerStatus::UNKNOWN:
      response.set_status(CreateLxdContainerResponse::UNKNOWN);
      break;
    case VirtualMachine::CreateLxdContainerStatus::CREATING:
      response.set_status(CreateLxdContainerResponse::CREATING);
      break;
    case VirtualMachine::CreateLxdContainerStatus::EXISTS:
      response.set_status(CreateLxdContainerResponse::EXISTS);
      break;
    case VirtualMachine::CreateLxdContainerStatus::FAILED:
      response.set_status(CreateLxdContainerResponse::FAILED);
      break;
  }
  response.set_failure_reason(error_msg);

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::StartLxdContainer(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received StartLxdContainer request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  StartLxdContainerRequest request;
  StartLxdContainerResponse response;
  response.set_status(StartLxdContainerResponse::UNKNOWN);

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse StartLxdRequest from message";
    response.set_failure_reason("unable to parse StartLxdRequest from message");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    response.set_failure_reason(base::StringPrintf(
        "requested VM does not exist: %s", request.vm_name().c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string container_name = request.container_name().empty()
                                   ? kDefaultContainerName
                                   : request.container_name();

  std::string container_private_key, host_public_key;
  std::string error_msg;
  if (!GetContainerSshKeys(request.owner_id(), request.vm_name(),
                           container_name, &host_public_key,
                           nullptr,  // host private key
                           nullptr,  // container public key
                           &container_private_key,
                           nullptr,  // hostname
                           &error_msg)) {
    response.set_failure_reason(error_msg);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  std::string container_token = vm->GenerateContainerToken(container_name);

  VirtualMachine::StartLxdContainerStatus status =
      vm->StartLxdContainer(container_name, container_private_key,
                            host_public_key, container_token, &error_msg);

  switch (status) {
    case VirtualMachine::StartLxdContainerStatus::UNKNOWN:
      response.set_status(StartLxdContainerResponse::UNKNOWN);
      break;
    case VirtualMachine::StartLxdContainerStatus::STARTED:
      response.set_status(StartLxdContainerResponse::STARTED);
      break;
    case VirtualMachine::StartLxdContainerStatus::RUNNING:
      response.set_status(StartLxdContainerResponse::RUNNING);
      break;
    case VirtualMachine::StartLxdContainerStatus::FAILED:
      response.set_status(StartLxdContainerResponse::FAILED);
      break;
  }

  response.set_failure_reason(error_msg);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::GetLxdContainerUsername(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received GetLxdContainerUsername request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  GetLxdContainerUsernameRequest request;
  GetLxdContainerUsernameResponse response;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse GetLxdContainerUsernameRequest from message";
    response.set_failure_reason(
        "unable to parse GetLxdContainerUsernameRequest from message");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    response.set_failure_reason(base::StringPrintf(
        "requested VM does not exist: %s", request.vm_name().c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string error_msg, username;
  VirtualMachine::GetLxdContainerUsernameStatus status =
      vm->GetLxdContainerUsername(request.container_name().empty()
                                      ? kDefaultContainerName
                                      : request.container_name(),
                                  &username, &error_msg);

  switch (status) {
    case VirtualMachine::GetLxdContainerUsernameStatus::UNKNOWN:
      response.set_status(GetLxdContainerUsernameResponse::UNKNOWN);
      break;
    case VirtualMachine::GetLxdContainerUsernameStatus::SUCCESS:
      response.set_status(GetLxdContainerUsernameResponse::SUCCESS);
      break;
    case VirtualMachine::GetLxdContainerUsernameStatus::CONTAINER_NOT_FOUND:
      response.set_status(GetLxdContainerUsernameResponse::CONTAINER_NOT_FOUND);
      break;
    case VirtualMachine::GetLxdContainerUsernameStatus::CONTAINER_NOT_RUNNING:
      response.set_status(
          GetLxdContainerUsernameResponse::CONTAINER_NOT_RUNNING);
      break;
    case VirtualMachine::GetLxdContainerUsernameStatus::USER_NOT_FOUND:
      response.set_status(GetLxdContainerUsernameResponse::USER_NOT_FOUND);
      break;
    case VirtualMachine::GetLxdContainerUsernameStatus::FAILED:
      response.set_status(GetLxdContainerUsernameResponse::FAILED);
      break;
  }

  response.set_username(username);
  response.set_failure_reason(error_msg);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::SetUpLxdContainerUser(
    dbus::MethodCall* method_call) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  LOG(INFO) << "Received SetUpLxdContainerUser request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  SetUpLxdContainerUserRequest request;
  SetUpLxdContainerUserResponse response;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse SetUpLxdContainerUserRequest from message";
    response.set_failure_reason(
        "unable to parse SetUpLxdContainerUserRequest from message");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  VirtualMachine* vm = FindVm(request.owner_id(), request.vm_name());
  if (!vm) {
    LOG(ERROR) << "Requested VM does not exist:" << request.vm_name();
    response.set_failure_reason(base::StringPrintf(
        "requested VM does not exist: %s", request.vm_name().c_str()));
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::string error_msg;
  VirtualMachine::SetUpLxdContainerUserStatus status =
      vm->SetUpLxdContainerUser(request.container_name().empty()
                                    ? kDefaultContainerName
                                    : request.container_name(),
                                request.container_username(), &error_msg);

  switch (status) {
    case VirtualMachine::SetUpLxdContainerUserStatus::UNKNOWN:
      response.set_status(SetUpLxdContainerUserResponse::UNKNOWN);
      break;
    case VirtualMachine::SetUpLxdContainerUserStatus::SUCCESS:
      response.set_status(SetUpLxdContainerUserResponse::SUCCESS);
      break;
    case VirtualMachine::SetUpLxdContainerUserStatus::EXISTS:
      response.set_status(SetUpLxdContainerUserResponse::EXISTS);
      break;
    case VirtualMachine::SetUpLxdContainerUserStatus::FAILED:
      response.set_status(SetUpLxdContainerUserResponse::FAILED);
      break;
  }
  response.set_failure_reason(error_msg);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

std::unique_ptr<dbus::Response> Service::GetDebugInformation(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received GetDebugInformation request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageWriter writer(dbus_response.get());
  GetDebugInformationResponse response;

  std::string container_debug_information;
  std::string* debug_information = response.mutable_debug_information();
  for (const auto& vm : vms_) {
    const std::string& vm_name = vm.first.second;
    *debug_information += "VM: ";
    *debug_information += vm_name;
    *debug_information += "\n";
    for (const auto& container_name : vm.second->GetContainerNames()) {
      *debug_information += "\tContainer: ";
      *debug_information += container_name;
      *debug_information += "\n";

      container_debug_information.clear();
      Container* container = vm.second->GetContainerForName(container_name);
      if (!container->GetDebugInformation(&container_debug_information)) {
        *debug_information += "\t\tfailed to get debug information\n";
        *debug_information += "\t\t";
        *debug_information += container_debug_information;
        *debug_information += "\n";
      } else {
        std::vector<base::StringPiece> info_lines = base::SplitStringPiece(
            container_debug_information, "\n", base::KEEP_WHITESPACE,
            base::SPLIT_WANT_NONEMPTY);
        for (const auto& line : info_lines) {
          *debug_information += "\t\t";
          line.AppendToString(debug_information);
          *debug_information += "\n";
        }
      }
    }
  }

  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

bool Service::GetVirtualMachineForContainerIp(uint32_t container_ip,
                                              VirtualMachine** vm_out,
                                              std::string* owner_id_out,
                                              std::string* name_out) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(vm_out);
  CHECK(owner_id_out);
  CHECK(name_out);
  for (const auto& vm : vms_) {
    const uint32_t netmask = vm.second->container_netmask();
    if ((vm.second->container_subnet() & netmask) != (container_ip & netmask)) {
      continue;
    }
    *owner_id_out = vm.first.first;
    *name_out = vm.first.second;
    *vm_out = vm.second.get();
    return true;
  }
  return false;
}

bool Service::GetVirtualMachineForCid(const uint32_t cid,
                                      VirtualMachine** vm_out,
                                      std::string* owner_id_out,
                                      std::string* name_out) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  CHECK(vm_out);
  CHECK(owner_id_out);
  CHECK(name_out);
  for (const auto& vm : vms_) {
    if (vm.second->cid() != cid) {
      continue;
    }
    *owner_id_out = vm.first.first;
    *name_out = vm.first.second;
    *vm_out = vm.second.get();
    return true;
  }
  return false;
}

void Service::StartSshForwarding(const std::string& owner_id,
                                 const std::string& ip,
                                 const std::string& username) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());

  std::string host_private_key, container_public_key, hostname;
  std::string error_msg;

  if (!GetContainerSshKeys(owner_id, kDefaultVmName, kDefaultContainerName,
                           nullptr,  // host public key
                           &host_private_key, &container_public_key,
                           nullptr,  // container private key
                           nullptr,  // hostname
                           &error_msg)) {
    LOG(ERROR) << "Failed to get keys for SSH forwarding: " << error_msg;
    return;
  }

  // Set up a known_hosts file and an identity file.
  base::FilePath ssh_dir(kRuntimeDir);
  base::File::Error dir_error;
  if (!base::DirectoryExists(ssh_dir) &&
      !base::CreateDirectoryAndGetError(ssh_dir, &dir_error)) {
    LOG(ERROR) << "Failed to create directory for cicerone SSH: "
               << base::File::ErrorToString(dir_error);
    return;
  }

  std::string known_hosts =
      base::StringPrintf("[%s]:%s %s", ip.c_str(), kContainerSshPort,
                         container_public_key.c_str());
  base::FilePath known_hosts_path =
      base::FilePath(kRuntimeDir).Append(kSshKnownHostsFilename);
  if (!base::WriteFile(known_hosts_path, known_hosts.c_str(),
                       known_hosts.length())) {
    LOG(ERROR) << "Failed to write to container SSH pubkey file";
    return;
  }

  base::FilePath identity_path = ssh_dir.Append(kSshIdentityFilename);
  if (!base::WriteFile(identity_path, host_private_key.c_str(),
                       host_private_key.length())) {
    LOG(ERROR) << "Failed to write to SSH identity file";
    return;
  }
  if (!base::SetPosixFilePermissions(identity_path,
                                     base::FILE_PERMISSION_READ_BY_USER |
                                         base::FILE_PERMISSION_WRITE_BY_USER)) {
    LOG(ERROR) << "Failed to set permissions on SSH identity file";
    return;
  }
  ssh_process_.Reset(0);

  ssh_process_.AddArg(kSshBin);

  // Specify the identity file.
  ssh_process_.AddArg("-i");
  ssh_process_.AddArg(identity_path.value());

  // Specify the known hosts file.
  ssh_process_.AddArg("-o");
  ssh_process_.AddArg(std::string("UserKnownHostsFile=") +
                      known_hosts_path.value());

  // Don't run a command; port forward only.
  ssh_process_.AddArg("-N");

  // cros-sftp uses a nonstandard port.
  ssh_process_.AddArg("-p");
  ssh_process_.AddArg(kContainerSshPort);

  for (const uint16_t port : kStaticForwardPorts) {
    ssh_process_.AddArg("-L");
    ssh_process_.AddArg(base::StringPrintf("%u:localhost:%u", port, port));
  }

  ssh_process_.AddArg(username + "@" + ip);

  if (!ssh_process_.Start()) {
    LOG(ERROR) << "Failed to start SSH process";
    return;
  }
}

bool Service::GetContainerSshKeys(const std::string& owner_id,
                                  const std::string& vm_name,
                                  const std::string& container_name,
                                  std::string* host_pubkey_out,
                                  std::string* host_privkey_out,
                                  std::string* container_pubkey_out,
                                  std::string* container_privkey_out,
                                  std::string* hostname_out,
                                  std::string* error_out) {
  DCHECK(error_out);
  // Request SSH keys from concierge.
  dbus::MethodCall method_call(vm_tools::concierge::kVmConciergeInterface,
                               vm_tools::concierge::kGetContainerSshKeysMethod);
  vm_tools::concierge::ContainerSshKeysRequest request;
  vm_tools::concierge::ContainerSshKeysResponse response;
  dbus::MessageWriter writer(&method_call);

  request.set_cryptohome_id(owner_id);
  request.set_vm_name(vm_name);
  request.set_container_name(container_name);
  writer.AppendProtoAsArrayOfBytes(request);
  std::unique_ptr<dbus::Response> dbus_response =
      concierge_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to get SSH keys from concierge";
    error_out->assign("failed to get SSH keys from concierge");
    return false;
  }
  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Unable to parse ContainerSshKeysResponse from message";
    error_out->assign("unable to parse ContainerSshKeysResponse from message");
    return false;
  }

  if (host_pubkey_out) {
    *host_pubkey_out = std::move(response.host_public_key());
  }
  if (host_privkey_out) {
    *host_privkey_out = std::move(response.host_private_key());
  }
  if (container_pubkey_out) {
    *container_pubkey_out = std::move(response.container_public_key());
  }
  if (container_privkey_out) {
    *container_privkey_out = std::move(response.container_private_key());
  }
  if (hostname_out) {
    *hostname_out = std::move(response.hostname());
  }

  return true;
}

void Service::RegisterHostname(const std::string& hostname,
                               const std::string& ip) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  dbus::MethodCall method_call(crosdns::kCrosDnsInterfaceName,
                               crosdns::kSetHostnameIpMappingMethod);
  dbus::MessageWriter writer(&method_call);
  // Params are hostname, IPv4, IPv6 (but we don't have IPv6 yet).
  writer.AppendString(hostname);
  writer.AppendString(ip);
  writer.AppendString("");
  std::unique_ptr<dbus::Response> dbus_response =
      crosdns_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    // If there's some issue with the resolver service, don't make that
    // propagate to a higher level failure and just log it. We have logic for
    // setting this up again if that service restarts.
    LOG(WARNING)
        << "Failed to send dbus message to crosdns to register hostname";
  } else {
    hostname_mappings_[hostname] = ip;
    if (hostname == kDefaultContainerHostname)
      linuxhost_ip_ = ip;
  }
}

void Service::UnregisterVmContainers(VirtualMachine* vm,
                                     const std::string& owner_id,
                                     const std::string& vm_name) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  if (!vm)
    return;
  // When we were in concierge, this method was important because we shared a
  // D-Bus thread with concierge who was stopping the VM. Now that we are in a
  // separate process, we should receive the gRPC call from the container for
  // container shutdown before we receive the D-Bus call from concierge for the
  // VM stopping. It is entirely possible that they come in out of order, so we
  // still need this in case that happens.
  std::vector<std::string> containers = vm->GetContainerNames();
  for (auto& container_name : containers) {
    LOG(WARNING) << "Latent container left in VM " << vm_name << " of "
                 << container_name;
    if (owner_id == primary_owner_id_) {
      UnregisterHostname(base::StringPrintf(
          "%s.%s.linux.test", container_name.c_str(), vm_name.c_str()));
      if (vm_name == kDefaultVmName &&
          container_name == kDefaultContainerName) {
        UnregisterHostname(kDefaultContainerHostname);
        ssh_process_.Reset(0);
      }
    }

    // Send the D-Bus signal to indicate the container has shutdown.
    dbus::Signal signal(kVmCiceroneInterface, kContainerShutdownSignal);
    ContainerShutdownSignal proto;
    proto.set_vm_name(vm_name);
    proto.set_container_name(container_name);
    proto.set_owner_id(owner_id);
    dbus::MessageWriter(&signal).AppendProtoAsArrayOfBytes(proto);
    exported_object_->SendSignal(&signal);
  }
}

void Service::UnregisterHostname(const std::string& hostname) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  dbus::MethodCall method_call(crosdns::kCrosDnsInterfaceName,
                               crosdns::kRemoveHostnameIpMappingMethod);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(hostname);
  std::unique_ptr<dbus::Response> dbus_response =
      crosdns_service_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    // If there's some issue with the resolver service, don't make that
    // propagate to a higher level failure and just log it. We have logic for
    // setting this up again if that service restarts.
    LOG(WARNING) << "Failed to send dbus message to crosdns to unregister "
                 << "hostname";
  }
  hostname_mappings_.erase(hostname);
  if (hostname == kDefaultContainerHostname)
    linuxhost_ip_ = "";
}

void Service::OnCrosDnsNameOwnerChanged(const std::string& old_owner,
                                        const std::string& new_owner) {
  DCHECK(sequence_checker_.CalledOnValidSequencedThread());
  if (!new_owner.empty()) {
    // Re-register everything in our map.
    for (auto& pair : hostname_mappings_) {
      RegisterHostname(pair.first, pair.second);
    }
  }
}

void Service::OnCrosDnsServiceAvailable(bool service_is_available) {
  if (service_is_available) {
    crosdns_service_proxy_->SetNameOwnerChangedCallback(base::Bind(
        &Service::OnCrosDnsNameOwnerChanged, weak_ptr_factory_.GetWeakPtr()));
  }
}

VirtualMachine* Service::FindVm(const std::string& owner_id,
                                const std::string& vm_name) {
  VmKey vm_key = std::make_pair(owner_id, vm_name);
  auto iter = vms_.find(vm_key);
  if (iter != vms_.end())
    return iter->second.get();
  if (!owner_id.empty()) {
    // TODO(jkardatzke): Remove this empty owner check once the other CLs land
    // for setting this everywhere.
    vm_key = std::make_pair("", vm_name);
    auto iter = vms_.find(vm_key);
    if (iter != vms_.end())
      return iter->second.get();
  }
  return nullptr;
}

}  // namespace cicerone
}  // namespace vm_tools
