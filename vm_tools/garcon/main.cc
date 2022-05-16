// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <limits.h>
#include <signal.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <memory>
#include <string>

// syslog.h and base/logging.h both try to #define LOG_INFO and LOG_WARNING.
// We need to #undef at least these two before including base/logging.h.  The
// others are included to be consistent.
namespace {
const int kSyslogDebug = LOG_DEBUG;
const int kSyslogInfo = LOG_INFO;
const int kSyslogWarning = LOG_WARNING;
const int kSyslogError = LOG_ERR;
const int kSyslogCritical = LOG_CRIT;

#undef LOG_INFO
#undef LOG_WARNING
#undef LOG_ERR
#undef LOG_CRIT
}  // namespace

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/command_line.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/synchronization/waitable_event.h>
#include <base/system/sys_info.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/task_runner.h>
#include <base/threading/thread.h>
#include <vm_protos/proto_bindings/container_guest.grpc.pb.h>
#include <chromeos/constants/vm_tools.h>

#include "google/protobuf/util/json_util.h"
#include "vm_tools/garcon/host_notifier.h"
#include "vm_tools/garcon/package_kit_proxy.h"
#include "vm_tools/garcon/service_impl.h"

constexpr char kLogPrefix[] = "garcon: ";
constexpr char kAllowAnyUserSwitch[] = "allow_any_user";
constexpr char kServerSwitch[] = "server";
constexpr char kClientSwitch[] = "client";
constexpr char kUrlSwitch[] = "url";
constexpr char kTerminalSwitch[] = "terminal";
constexpr char kSelectFileSwitch[] = "selectfile";
constexpr char kSelectFileTypeSwitch[] = "type";
constexpr char kSelectFileTitleSwitch[] = "title";
constexpr char kSelectFilePathSwitch[] = "path";
constexpr char kSelectFileExtensionsSwitch[] = "extensions";
constexpr char kDiskSwitch[] = "disk";
constexpr char kGetDiskInfoArg[] = "get_disk_info";
constexpr char kRequestSpaceArg[] = "request_space";
constexpr char kReleaseSpaceArg[] = "release_space";
constexpr uint32_t kVsockPortStart = 10000;
constexpr uint32_t kVsockPortEnd = 20000;

constexpr uid_t kCrostiniDefaultUid = 1000;

bool LogToSyslog(logging::LogSeverity severity,
                 const char* /* file */,
                 int /* line */,
                 size_t message_start,
                 const std::string& message) {
  switch (severity) {
    case logging::LOGGING_INFO:
      severity = kSyslogInfo;
      break;
    case logging::LOGGING_WARNING:
      severity = kSyslogWarning;
      break;
    case logging::LOGGING_ERROR:
      severity = kSyslogError;
      break;
    case logging::LOGGING_FATAL:
      severity = kSyslogCritical;
      break;
    default:
      severity = kSyslogDebug;
      break;
  }
  syslog(severity, "%s", message.c_str() + message_start);

  return true;
}

void RunGarconService(vm_tools::garcon::PackageKitProxy* pk_proxy,
                      base::WaitableEvent* event,
                      std::shared_ptr<grpc::Server>* server_copy,
                      int* vsock_listen_port,
                      scoped_refptr<base::TaskRunner> task_runner,
                      vm_tools::garcon::HostNotifier* host_notifier) {
  // We don't want to receive SIGTERM on this thread.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // See crbug.com/922694 for more reference.
  // There's a bug in our patched version of gRPC where it uses signed integers
  // for ports. VSOCK uses unsigned integers for ports. So if we let the kernel
  // choose the port for us, then it can end up choosing one that has the high
  // bit set and cause gRPC to assert on the negative port number. This was a
  // much easier solution than patching gRPC or updating the kernel to keep the
  // VSOCK ports in the signed integer range.
  // The end on this for loop only exists to prevent running forever in case
  // something else goes wrong.
  for (*vsock_listen_port = kVsockPortStart; *vsock_listen_port < kVsockPortEnd;
       ++(*vsock_listen_port)) {
    // Build the server.
    grpc::ServerBuilder builder;
    builder.AddListeningPort(
        base::StringPrintf("vsock:%u:%d", VMADDR_CID_ANY, *vsock_listen_port),
        grpc::InsecureServerCredentials(), nullptr);

    vm_tools::garcon::ServiceImpl garcon_service(pk_proxy, task_runner.get(),
                                                 host_notifier);
    builder.RegisterService(&garcon_service);

    std::shared_ptr<grpc::Server> server(builder.BuildAndStart().release());
    if (!server) {
      LOG(WARNING) << "garcon failed binding requested vsock port "
                   << *vsock_listen_port << ", trying again with a new port";
      continue;
    }

    *server_copy = server;
    event->Signal();

    LOG(INFO) << "Server listening on vsock port " << *vsock_listen_port;
    // The following call will return once we invoke Shutdown on the gRPC
    // server when the main RunLoop exits.
    server->Wait();
    break;
  }
}

void CreatePackageKitProxy(
    base::WaitableEvent* event,
    vm_tools::garcon::HostNotifier* host_notifier,
    std::unique_ptr<vm_tools::garcon::PackageKitProxy>* proxy_ptr) {
  // We don't want to receive SIGTERM on this thread.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  *proxy_ptr = vm_tools::garcon::PackageKitProxy::Create(host_notifier);
  event->Signal();
}

void PrintUsage() {
  LOG(INFO) << "Garcon: VM container bridge for Chrome OS\n\n"
            << "Mode Switches (must use one):\n"
            << "Mode Switch:\n"
            << "  --server: run in background as daemon\n"
            << "  --client: run as client and send message to host\n"
            << "Client Switches (only with --client):\n"
            << "  --url: opens all arguments as URLs in host browser\n"
            << "  --terminal: opens terminal\n"
            << "  --selectfile: open file dialog and return file: URL list\n"
            << "  --disk: handles requests relating to disk management\n"
            << "Select File Switches (only with --client --selectfile):\n"
            << "  --type: "
               "open-file|open-multi-file|saveas-file|folder|upload-folder\n"
            << "  --title: title for dialog\n"
            << "  --path: default path (file: URL or path)\n"
            << "  --extensions: comma-separated list of allowed extensions\n"
            << "Disk args (use with --client --disk):\n"
            << "  get_disk_info: returns information about the disk\n"
            << "  request_space <bytes>: tries to expand the disk by <bytes>\n"
            << "  release_space <bytes>: tries to shrink the disk by <bytes>\n"
            << "Server Switches (only with --server):\n"
            << "  --allow_any_user: allow running as non-default uid\n";
}

int HandleDiskArgs(std::vector<std::string> args) {
  std::string output;
  if (args.empty()) {
    LOG(ERROR) << "Missing arguments in --disk mode";
    PrintUsage();
    return -1;
  }
  google::protobuf::util::JsonOptions options;
  options.always_print_primitive_fields = true;
  if (args.at(0) == kGetDiskInfoArg) {
    vm_tools::container::GetDiskInfoResponse response;
    vm_tools::garcon::HostNotifier::GetDiskInfo(&response);
    // Error code 4 is for invalid requests; those that have incomplete meta
    // data, don't originate from Borealis or are made when Chrome infra isn't
    // set up. To support unorthodox workflows, we return basic information,
    // rather than an error.
    if (response.error() == 4) {
      response.set_error(0);
      int free_space =
          base::SysInfo::AmountOfFreeDiskSpace(base::FilePath("/mnt/stateful"));
      response.set_available_space(free_space);
      // TODO(b/223308797): Potentially revert this to being empty.
      response.set_expandable_space(free_space);
    }
    google::protobuf::util::MessageToJsonString(response, &output, options);
    std::cout << output << std::endl;
    if (response.error() == 0)
      return 0;
    LOG(WARNING) << "Something went wrong when requesting disk info";
    return -1;
  }
  if (args.size() < 2) {
    LOG(ERROR) << "Missing additional argument for request/release space";
    PrintUsage();
    return -1;
  }
  uint64_t space_arg;
  bool arg_conversion = base::StringToUint64(args.at(1), &space_arg);
  if (args.at(0) == kRequestSpaceArg) {
    vm_tools::container::RequestSpaceResponse response;
    if (arg_conversion) {
      vm_tools::garcon::HostNotifier::RequestSpace(space_arg, &response);
    } else {
      LOG(WARNING) << "Couldn't parse requested_bytes (expected Uint64)";
      PrintUsage();
      response.set_error(1);
    }
    google::protobuf::util::MessageToJsonString(response, &output, options);
    std::cout << output << std::endl;
    if (response.error() == 0)
      return 0;
    LOG(WARNING) << "Something went wrong when requesting for more space";
    return -1;
  }
  if (args.at(0) == kReleaseSpaceArg) {
    vm_tools::container::ReleaseSpaceResponse response;
    if (arg_conversion) {
      vm_tools::garcon::HostNotifier::ReleaseSpace(space_arg, &response);
    } else {
      LOG(WARNING) << "Couldn't parse bytes_to_release (expected Uint64)";
      PrintUsage();
      response.set_error(1);
    }
    google::protobuf::util::MessageToJsonString(response, &output, options);
    std::cout << output << std::endl;
    if (response.error() == 0)
      return 0;
    LOG(WARNING) << "Something went wrong when releasing disk space";
    return -1;
  }
  LOG(ERROR) << "Invalid disk request";
  PrintUsage();
  return -1;
}

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  logging::InitLogging(logging::LoggingSettings());

  bool serverMode = cl->HasSwitch(kServerSwitch);
  bool clientMode = cl->HasSwitch(kClientSwitch);
  // The standard says that bool to int conversion is implicit and that
  // false => 0 and true => 1.
  // clang-format off
  if (serverMode + clientMode != 1) {
    // clang-format on
    LOG(ERROR) << "Exactly one of --server or --client must be used.";
    PrintUsage();
    return -1;
  }

  if (clientMode) {
    if (cl->HasSwitch(kUrlSwitch)) {
      std::vector<std::string> args = cl->GetArgs();
      if (args.empty()) {
        LOG(ERROR) << "Missing URL arguments in --url mode";
        PrintUsage();
        return -1;
      }
      // All arguments are URLs, send them to the host to be opened. The host
      // will do its own verification for validity of the URLs.
      for (const auto& arg : args) {
        if (!vm_tools::garcon::HostNotifier::OpenUrlInHost(arg)) {
          return -1;
        }
      }
      return 0;
    } else if (cl->HasSwitch(kTerminalSwitch)) {
      std::vector<std::string> args = cl->GetArgs();
      if (vm_tools::garcon::HostNotifier::OpenTerminal(std::move(args)))
        return 0;
      else
        return -1;
    } else if (cl->HasSwitch(kSelectFileSwitch)) {
      std::string type = cl->GetSwitchValueNative(kSelectFileTypeSwitch);
      std::string title = cl->GetSwitchValueNative(kSelectFileTitleSwitch);
      std::string path = cl->GetSwitchValueNative(kSelectFilePathSwitch);
      std::string extensions =
          cl->GetSwitchValueNative(kSelectFileExtensionsSwitch);
      std::vector<std::string> files;
      if (vm_tools::garcon::HostNotifier::SelectFile(type, title, path,
                                                     extensions, &files)) {
        for (const auto& file : files) {
          std::cout << file << std::endl;
        }
        return 0;
      } else {
        return -1;
      }
    } else if (cl->HasSwitch(kDiskSwitch)) {
      return HandleDiskArgs(cl->GetArgs());
    }
    LOG(ERROR) << "Missing client switch for client mode.";
    PrintUsage();
    return -1;
  }

  // Set up logging to syslog for server mode.
  openlog(kLogPrefix, LOG_PID, LOG_DAEMON);
  logging::SetLogMessageHandler(LogToSyslog);

  // Exit if not running as the container default user.
  if (getuid() != kCrostiniDefaultUid && !cl->HasSwitch(kAllowAnyUserSwitch)) {
    LOG(ERROR) << "garcon normally runs only as uid(" << kCrostiniDefaultUid
               << "). Use --allow_any_user to override";
    return -1;
  }

  // Note on threading model. There are 4 threads used in garcon. One is for the
  // incoming gRPC requests. One is for the D-Bus communication with the
  // PackageKit daemon. The third is the main thread which is for gRPC requests
  // to the host as well as for monitoring filesystem changes (which result in a
  // gRPC call to the host under certain conditions). The main thing to be
  // careful of is that the gRPC thread for incoming requests is never blocking
  // on the gRPC thread for outgoing requests (since they are both talking to
  // cicerone, and both of those operations in cicerone are likely going to use
  // the same D-Bus thread for communication within cicerone). The fourth thread
  // is for running tasks initiated by garcon service.

  // Thread that the gRPC server is running on.
  base::Thread grpc_thread{"gRPC Server Thread"};
  if (!grpc_thread.Start()) {
    LOG(ERROR) << "Failed starting the gRPC thread";
    return -1;
  }

  // Thread that D-Bus communication runs on.
  base::Thread dbus_thread{"D-Bus Thread"};
  if (!dbus_thread.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOG(ERROR) << "Failed starting the D-Bus thread";
    return -1;
  }

  // Thread that tasks started from garcon service run on.
  // Specifically, Ansible playbook application runs on
  // |garcon_service_tasks_thread|.
  base::Thread garcon_service_tasks_thread{"Garcon Service Tasks Thread"};
  if (!garcon_service_tasks_thread.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOG(ERROR) << "Failed starting the garcon service tasks thread";
    return -1;
  }

  // Setup the HostNotifier on the run loop for the main thread. It needs to
  // have its own run loop separate from the gRPC server & D-Bus server since it
  // will be using base::FilePathWatcher to identify installed application and
  // mime type changes.
  base::RunLoop run_loop;

  std::unique_ptr<vm_tools::garcon::HostNotifier> host_notifier =
      vm_tools::garcon::HostNotifier::Create(run_loop.QuitClosure());
  if (!host_notifier) {
    LOG(ERROR) << "Failure setting up the HostNotifier";
    return -1;
  }

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  // This needs to be created on the D-Bus thread.
  std::unique_ptr<vm_tools::garcon::PackageKitProxy> pk_proxy;
  bool ret = dbus_thread.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&CreatePackageKitProxy, &event,
                                host_notifier.get(), &pk_proxy));
  if (!ret) {
    LOG(ERROR) << "Failed to post PackageKit proxy creation to D-Bus thread";
    return -1;
  }
  // Wait for the creation to complete.
  event.Wait();
  if (!pk_proxy) {
    LOG(ERROR) << "Failed in creating the PackageKit proxy";
    return -1;
  }
  event.Reset();

  // Launch the gRPC server on the gRPC thread.
  std::shared_ptr<grpc::Server> server_copy;
  int vsock_listen_port = 0;
  ret = grpc_thread.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&RunGarconService, pk_proxy.get(), &event,
                                &server_copy, &vsock_listen_port,
                                garcon_service_tasks_thread.task_runner(),
                                host_notifier.get()));
  if (!ret) {
    LOG(ERROR) << "Failed to post server startup task to grpc thread";
    return -1;
  }

  // Wait for the gRPC server to start.
  event.Wait();

  if (!server_copy) {
    LOG(ERROR) << "gRPC server failed to start";
    return -1;
  }

  if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
    PLOG(ERROR) << "Unable to explicitly ignore SIGCHILD";
    return -1;
  }

  if (!host_notifier->Init(static_cast<uint32_t>(vsock_listen_port),
                           pk_proxy.get())) {
    LOG(ERROR) << "Failed to set up host notifier";
    return -1;
  }

  // Start the main run loop now for the HostNotifier.
  run_loop.Run();

  // We get here after a SIGTERM gets posted and the main run loop has exited.
  // We then shutdown the gRPC server (which will terminate that thread) and
  // then stop the D-Bus thread. We will be the only remaining thread at that
  // point so everything can be safely destructed and we remove the need for
  // any weak pointers.
  server_copy->Shutdown();
  dbus_thread.Stop();
  garcon_service_tasks_thread.Stop();
  return 0;
}
