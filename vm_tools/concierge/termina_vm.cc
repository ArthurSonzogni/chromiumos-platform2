// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/termina_vm.h"

#include <arpa/inet.h>
#include <linux/capability.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#include <optional>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/span.h>
#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/guid.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <base/time/time.h>
#include <google/protobuf/repeated_field.h>
#include <chromeos/constants/vm_tools.h>
#include <chromeos/patchpanel/net_util.h>
#include <grpcpp/grpcpp.h>
#include <sys/epoll.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/concierge/future.h"
#include "vm_tools/concierge/sibling_vms.h"
#include "vm_tools/concierge/tap_device_builder.h"
#include "vm_tools/concierge/vm_builder.h"
#include "vm_tools/concierge/vm_permission_interface.h"
#include "vm_tools/concierge/vm_util.h"
#include "vm_tools/concierge/vm_wl_interface.h"

using std::string;

namespace vm_tools {
namespace concierge {
namespace {

// Features to enable.
constexpr StartTerminaRequest_Feature kEnabledTerminaFeatures[] = {
    StartTerminaRequest::LXD_4_LTS};

// Name of the control socket used for controlling crosvm.
constexpr char kCrosvmSocket[] = "crosvm.sock";

// How long to wait before timing out on shutdown RPCs.
constexpr int64_t kShutdownTimeoutSeconds = 30;

// How long to wait before timing out on StartTermina RPCs.
constexpr int64_t kStartTerminaTimeoutSeconds = 150;

// How long to wait before timing out on regular RPCs.
constexpr int64_t kDefaultTimeoutSeconds = 10;

// How long to wait before timing out on child process exits.
constexpr base::TimeDelta kChildExitTimeout = base::Seconds(10);

// Offset in a subnet of the gateway/host.
constexpr size_t kHostAddressOffset = 0;

// Offset in a subnet of the client/guest.
constexpr size_t kGuestAddressOffset = 1;

// The maximum GPU shader cache disk usage, interpreted by Mesa. For details
// see MESA_GLSL_CACHE_MAX_SIZE at https://docs.mesa3d.org/envvars.html.
constexpr char kGpuCacheSizeString[] = "50M";
constexpr char kRenderServerCacheSizeString[] = "50M";

// The maximum render server shader cache disk usage for borealis.
// TODO(b/169802596): Set cache size in a smarter way.
// See b/209849605#comment5 for borealis cache size reasoning.
constexpr char kGpuCacheSizeStringBorealis[] = "1000M";
constexpr char kRenderServerCacheSizeStringBorealis[] = "1000M";

// Special value to represent an invalid disk index for `crosvm disk`
// operations.
constexpr int kInvalidDiskIndex = -1;

std::unique_ptr<patchpanel::Subnet> MakeSubnet(
    const patchpanel::Client::IPv4Subnet& subnet) {
  if (subnet.base_addr.size() != 4) {
    return nullptr;
  }
  uint32_t addr =
      patchpanel::Ipv4Addr(subnet.base_addr[0], subnet.base_addr[1],
                           subnet.base_addr[2], subnet.base_addr[3]);
  return std::make_unique<patchpanel::Subnet>(addr, subnet.prefix_len,
                                              base::DoNothing());
}

std::optional<std::vector<std::unique_ptr<brillo::ProcessImpl>>>
StartSiblingVvuDevices(std::vector<base::StringPairs> vvu_device_cmds) {
  std::vector<std::unique_ptr<brillo::ProcessImpl>> vvu_device_processes;
  for (base::StringPairs cmd : vvu_device_cmds) {
    auto process = std::make_unique<brillo::ProcessImpl>();
    std::string command_line_for_log{};

    for (std::pair<std::string, std::string>& arg : cmd) {
      command_line_for_log += arg.first;
      command_line_for_log += " ";

      process->AddArg(std::move(arg.first));
      if (!arg.second.empty()) {
        command_line_for_log += arg.second;
        command_line_for_log += " ";
        process->AddArg(std::move(arg.second));
      }
    }
    LOG(INFO) << "Invoking VVU device: " << command_line_for_log;
    if (!process->Start()) {
      PLOG(ERROR) << "Failed to start VVU device";
      return std::nullopt;
    }
    vvu_device_processes.push_back(std::move(process));
  }

  return vvu_device_processes;
}

}  // namespace

TerminaVm::TerminaVm(
    uint32_t vsock_cid,
    std::unique_ptr<patchpanel::Client> network_client,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    VmMemoryId vm_memory_id,
    base::FilePath log_path,
    std::string stateful_device,
    uint64_t stateful_size,
    int64_t mem_mib,
    VmFeatures features,
    dbus::ObjectProxy* vm_permission_service_proxy,
    scoped_refptr<dbus::Bus> bus,
    VmId id,
    VmInfo::VmType classification,
    base::Thread* dbus_thread,
    std::unique_ptr<ScopedWlSocket> socket)
    : VmBaseImpl(std::move(network_client),
                 vsock_cid,
                 std::move(seneschal_server_proxy),
                 kCrosvmSocket,
                 std::move(runtime_dir),
                 vm_memory_id),
      features_(features),
      stateful_device_(stateful_device),
      stateful_size_(stateful_size),
      stateful_resize_type_(DiskResizeType::NONE),
      mem_mib_(mem_mib),
      log_path_(std::move(log_path)),
      id_(id),
      bus_(bus),
      vm_permission_service_proxy_(vm_permission_service_proxy),
      classification_(classification),
      manatee_client_(
          std::make_unique<org::chromium::ManaTEEInterfaceProxy>(bus_)),
      dbus_thread_(dbus_thread),
      socket_(std::move(socket)) {}

// For testing.
TerminaVm::TerminaVm(
    std::unique_ptr<patchpanel::Subnet> subnet,
    uint32_t vsock_cid,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    base::FilePath log_path,
    std::string stateful_device,
    uint64_t stateful_size,
    int64_t mem_mib,
    VmFeatures features,
    VmInfo::VmType classification,
    base::Thread* dbus_thread)
    : VmBaseImpl(nullptr /* network_client */,
                 vsock_cid,
                 std::move(seneschal_server_proxy),
                 "" /* cros_vm_socket */,
                 std::move(runtime_dir),
                 0 /* vm_memory_id */),
      subnet_(std::move(subnet)),
      features_(features),
      stateful_device_(stateful_device),
      stateful_size_(stateful_size),
      stateful_resize_type_(DiskResizeType::NONE),
      mem_mib_(mem_mib),
      log_path_(std::move(log_path)),
      id_(VmId("foo", "bar")),
      classification_(classification),
      dbus_thread_(dbus_thread) {
  CHECK(subnet_);
}

TerminaVm::~TerminaVm() {
  Shutdown();
}

std::unique_ptr<TerminaVm> TerminaVm::Create(
    uint32_t vsock_cid,
    std::unique_ptr<patchpanel::Client> network_client,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    VmMemoryId vm_memory_id,
    base::FilePath log_path,
    std::string stateful_device,
    uint64_t stateful_size,
    int64_t mem_mib,
    VmFeatures features,
    dbus::ObjectProxy* vm_permission_service_proxy,
    scoped_refptr<dbus::Bus> bus,
    VmId id,
    VmInfo::VmType classification,
    VmBuilder vm_builder,
    base::Thread* dbus_thread,
    std::unique_ptr<ScopedWlSocket> socket) {
  auto vm = base::WrapUnique(new TerminaVm(
      vsock_cid, std::move(network_client), std::move(seneschal_server_proxy),
      std::move(runtime_dir), vm_memory_id, std::move(log_path),
      std::move(stateful_device), std::move(stateful_size), mem_mib, features,
      vm_permission_service_proxy, std::move(bus), std::move(id),
      classification, dbus_thread, std::move(socket)));

  if (!vm->Start(std::move(vm_builder)))
    vm.reset();

  return vm;
}

std::string TerminaVm::GetCrosVmSerial(std::string hardware,
                                       std::string console_type) const {
  std::string common_params = "hardware=" + hardware;
  if (console_type != "") {
    common_params += "," + console_type + "=true";
  }
  if (hardware != "debugcon") {
    common_params += ",num=1";
  }
  if (log_path_.empty()) {
    return common_params + ",type=syslog";
  }
  return common_params + ",type=unix,path=" + log_path_.value();
}

bool TerminaVm::Start(VmBuilder vm_builder) {
  // Get the network interface.
  patchpanel::Client::IPv4Subnet container_subnet;
  if (!network_client_->NotifyTerminaVmStartup(vsock_cid_, &network_device_,
                                               &container_subnet)) {
    LOG(ERROR) << "No network devices available";
    return false;
  }

  // Sommelier relies on implicit modifier, which does not pass host modifier to
  // zwp_linux_buffer_params_v1_add. Graphics will be broken if modifiers are
  // enabled.  Sommelier shall be fixed to mirror what arc wayland_service does,
  // and then we can re-enable UBWC here.
  //
  // See b/229147702
  setenv("MINIGBM_DEBUG", "nocompression", 0);

  // TODO(b/193370101) Remove borealis specific code once crostini uses
  // permission service.
  if (classification_ == VmInfo::BOREALIS) {
    // Register the VM with permission service and obtain permission
    // token.
    if (!vm_permission::RegisterVm(bus_, vm_permission_service_proxy_, id_,
                                   vm_permission::VmType::BOREALIS,
                                   &permission_token_)) {
      LOG(ERROR) << "Failed to register with permission service";
      // TODO(lqu): Add "return false;" after chrome uprevs.
    }
  }

  subnet_ = MakeSubnet(network_device_.ipv4_subnet);
  if (!subnet_) {
    LOG(ERROR) << "Failed to read IPv4 subnet assigned to VM";
    return false;
  }
  container_subnet_ = MakeSubnet(container_subnet);
  if (!container_subnet_) {
    LOG(ERROR) << "Failed to read IPv4 subnet assigned to container guest";
    return false;
  }

  // Open the tap device.
  base::ScopedFD tap_fd = OpenTapDevice(
      network_device_.ifname, true /*vnet_hdr*/, nullptr /*ifname_out*/);
  if (!tap_fd.is_valid()) {
    LOG(ERROR) << "Unable to open and configure TAP device "
               << network_device_.ifname;
    return false;
  }

  vm_builder.AppendTapFd(std::move(tap_fd))
      .SetVsockCid(vsock_cid_)
      .SetSocketPath(GetVmSocketPath())
      .SetMemory(std::to_string(mem_mib_))
      .AppendSerialDevice(GetCrosVmSerial("serial", "earlycon"))
      .AppendSerialDevice(GetCrosVmSerial("virtio-console", "console"))
      .AppendSerialDevice(GetCrosVmSerial("debugcon", ""))
      .SetSyslogTag(base::StringPrintf("VM(%u)", vsock_cid_));

  if (features_.gpu) {
    vm_builder.EnableGpu(true)
        .EnableVulkan(features_.vulkan)
        .EnableBigGl(features_.big_gl)
        .EnableVirtgpuNativeContext(features_.virtgpu_native_context);

    if (classification_ == VmInfo::BOREALIS) {
      vm_builder.SetGpuCacheSize(kGpuCacheSizeStringBorealis);
      // For Borealis, place the render server process in
      // the GPU server cpuset cgroup.
      vm_builder.AppendCustomParam("--gpu-server-cgroup-path",
                                   kBorealisGpuServerCpusetCgroup);
    } else {
      vm_builder.SetGpuCacheSize(kGpuCacheSizeString);
    }

    if (features_.render_server) {
      vm_builder.EnableRenderServer(true);
      if (classification_ == VmInfo::BOREALIS) {
        vm_builder.SetRenderServerCacheSize(
            kRenderServerCacheSizeStringBorealis);
      } else {
        vm_builder.SetRenderServerCacheSize(kRenderServerCacheSizeString);
      }
    }
  }

  // Enable dGPU passthrough argument is only supported on Borealis VM.
  if (features_.dgpu_passthrough) {
    if (classification_ == VmInfo::BOREALIS) {
      vm_builder.EnableDGpuPassthrough(true);
    } else {
      LOG(ERROR) << "--enable-dgpu-passthrough is only supported on Borealis.";
      return false;
    }
  }

  if (features_.software_tpm)
    vm_builder.EnableSoftwareTpm(true /* enable */);

  if (features_.vtpm_proxy)
    vm_builder.EnableVtpmProxy(true /* enable */);

  // TODO(b/193370101) Remove borealis specific code once crostini uses
  // permission service.
  if (classification_ == VmInfo::BOREALIS) {
    if (vm_permission::IsMicrophoneEnabled(bus_, vm_permission_service_proxy_,
                                           permission_token_)) {
      vm_builder.AppendAudioDevice(
          VmBuilder::AudioDeviceType::kVirtio,
          "capture=true,backend=cras,client_type=borealis,"
          "socket_type=unified,num_output_devices=3,num_input_devices=3,"
          "num_output_streams=5,num_input_streams=5");
    } else {
      vm_builder.AppendAudioDevice(
          VmBuilder::AudioDeviceType::kVirtio,
          "backend=cras,client_type=borealis,socket_type=unified,"
          "num_output_devices=3,num_input_devices=3,"
          "num_output_streams=5,num_input_streams=5");
    }
  } else {
    if (features_.audio_capture) {
      vm_builder.AppendAudioDevice(
          VmBuilder::AudioDeviceType::kVirtio,
          "capture=true,backend=cras,socket_type=unified");
    } else {
      vm_builder.AppendAudioDevice(VmBuilder::AudioDeviceType::kVirtio,
                                   "backend=cras,socket_type=unified");
    }
  }

  for (const std::string& p : features_.kernel_params)
    vm_builder.AppendKernelParam(p);

  for (const std::string& s : features_.oem_strings)
    vm_builder.AppendOemString(s);

  // Switch off kmsg throttling so we can log all relevant startup messages
  vm_builder.AppendKernelParam("printk.devkmsg=on");

  // Change the process group before exec so that crosvm sending SIGKILL to the
  // whole process group doesn't kill us as well. The function also changes the
  // cpu cgroup for Termina crosvm processes.
  process_.SetPreExecCallback(base::BindOnce(
      &SetUpCrosvmProcess, base::FilePath(kTerminaCpuCgroup).Append("tasks")));

  if (USE_CROSVM_SIBLINGS) {
    auto cmds = vm_builder.BuildSiblingCmds(GetVvuDevicesInfo(), runtime_dir_);
    if (!cmds) {
      LOG(ERROR) << "Failed to build sibling VM commands";
      return false;
    }

    // TODO(b/196186396): There needs to be a way of knowing when the devices
    // have booted up before we start the sibling VM.
    if (!StartSiblingVm(cmds->vvu_cmds, cmds->sibling_cmd_args)) {
      LOG(ERROR) << "Failed to start termina as a sibling";
      return false;
    }
  } else {
    if (!StartProcess(vm_builder.BuildVmArgs()))
      return false;
  }

  // Create a stub for talking to the maitre'd instance inside the VM.
  stub_ = std::make_unique<vm_tools::Maitred::Stub>(grpc::CreateChannel(
      base::StringPrintf("vsock:%u:%u", vsock_cid_, vm_tools::kMaitredPort),
      grpc::InsecureChannelCredentials()));

  return true;
}

bool TerminaVm::SetTimezone(const std::string& timezone,
                            std::string* out_error) {
  if (!stub_) {
    *out_error = "maitred stub not initialized";
    return false;
  }

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  ::vm_tools::SetTimezoneRequest request;
  request.set_timezone_name(timezone);
  // Borealis needs timezone info to be bind-mounted due to Steam bug, see
  // TODO(b/237960004): Clean up this exception once Steam bug is fixed.
  request.set_use_bind_mount(GetInfo().type == VmInfo::BOREALIS);
  ::vm_tools::EmptyMessage response;

  auto result = stub_->SetTimezone(&ctx, request, &response);
  if (result.ok()) {
    *out_error = "";
    return true;
  }

  *out_error = result.error_message();
  return false;
}

bool TerminaVm::StartSiblingVm(std::vector<base::StringPairs> vvu_device_cmds,
                               std::vector<std::string> sibling_vm_args) {
  // First start the VVU device backend processes. Without these the VM won't
  // boot.
  auto vvu_device_processes = StartSiblingVvuDevices(vvu_device_cmds);
  if (!vvu_device_processes) {
    LOG(ERROR) << "Failed to start VVU device processes for the sibling VM";
    return false;
  }

  std::string sibling_args;
  for (std::string arg : sibling_vm_args) {
    sibling_args += " " + arg;
  }
  LOG(INFO) << "Send command to start sibling VM: " << sibling_args;

  int32_t error_code;
  base::ScopedFD fd_in;
  base::ScopedFD fd_out;
  brillo::ErrorPtr error;
  bool vm_started =
      AsyncNoReject(dbus_thread_->task_runner(),
                    base::BindOnce(
                        [](org::chromium::ManaTEEInterfaceProxy* manatee_client,
                           const std::vector<std::string>& args,
                           int32_t* error_code, base::ScopedFD* fd_in,
                           base::ScopedFD* fd_out, brillo::ErrorPtr* error) {
                          return manatee_client->StartTEEApplication(
                              "termina", args, false /* allow_unverified */,
                              error_code, fd_in, fd_out, error);
                        },
                        manatee_client_.get(), sibling_vm_args, &error_code,
                        &fd_in, &fd_out, &error))
          .Get()
          .val;

  // The |fd_in| is for writing from us. Watching it as readable will trigger an
  // event if the sibling is shutdown from under us and |fd_in|'s socket connect
  // is closed.
  auto fd_in_watcher = base::FileDescriptorWatcher::WatchReadable(
      fd_in.get(), base::BindRepeating(&TerminaVm::OnFdToSiblingReadable,
                                       base::Unretained(this)));
  sibling_state_ = std::make_optional(
      SiblingState{.fd_in = std::move(fd_in),
                   .fd_out = std::move(fd_out),
                   .fd_in_watcher = std::move(fd_in_watcher),
                   .vvu_device_processes = std::move(*vvu_device_processes)});
  LOG(INFO) << "Sibling Vm Started: " << vm_started;
  return vm_started;
}

bool TerminaVm::ShutdownSiblingVm() {
  if (!sibling_state_.has_value()) {
    LOG(INFO) << "No sibling VM exists to shutdown";
    return true;
  }

  // Shutting down involves two parts -
  // - Sending a Shutdown gRPC to the VM.
  // - epoll the sibling's output fd for a peer closed connection
  // event i.e. EPOLLRDHUP. If we detect this then we can safely
  // say the VM has shutdown.

  // For epoll detection add the sibling FD to the epoll set before the
  // shutdown gRPC is sent to the VM.
  base::ScopedFD sibling_vm_alive_epoll_fd(epoll_create1(0));
  if (!sibling_vm_alive_epoll_fd.is_valid()) {
    PLOG(ERROR) << "Failed to create epoll fd for the sibling "
                   "VM alive check";
    return false;
  }

  struct epoll_event ep_event {
    .events = EPOLLRDHUP, .data.u32 = 0,
  };
  if (epoll_ctl(sibling_vm_alive_epoll_fd.get(), EPOLL_CTL_ADD,
                sibling_state_->fd_out.get(), &ep_event) < 0) {
    PLOG(ERROR) << "Failed to epoll add sibling VM alive check fd";
    return false;
  }

  grpc::Status status = SendVMShutdownMessage();
  if (!status.ok()) {
    LOG(WARNING) << "Shutdown RPC failed for VM " << vsock_cid_
                 << " with error "
                 << "code " << status.error_code() << ": "
                 << status.error_message();
    return false;
  }

  // TODO(b/236747571): If guest shutdown fails or is not detected with in the
  // given timeout, ask Dugong to kill the crosvm process as a fail safe.
  if (epoll_wait(sibling_vm_alive_epoll_fd.get(), &ep_event, 1,
                 (kShutdownTimeoutSeconds *
                  base::Time::kMillisecondsPerSecond)) <= 0) {
    PLOG(ERROR) << "Failed to wait for sibling VM shutdown";
    return false;
  }

  // Ensure that the stream socket peer connection closed and no
  // other implicit event caused the epoll_wait call to return.
  if ((ep_event.events & EPOLLRDHUP) == 0) {
    LOG(ERROR) << "Failed to detect sibling VM fd closure. event: "
               << ep_event.events;
    return false;
  }

  // At this point the VM has shutdown, delete its state.
  sibling_state_.reset();
  LOG(INFO) << "Sibling VM shutdown complete";
  return true;
}

grpc::Status TerminaVm::SendVMShutdownMessage() {
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kShutdownTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::EmptyMessage empty;
  return stub_->Shutdown(&ctx, empty, &empty);
}

void TerminaVm::OnFdToSiblingReadable() {
  // Peek the sibling's socket to detect a disconnection. We never write to
  // |fd_in| so this event means the sibling has died. However, we peek and make
  // sure that that is the case. EAGAIN means that the socket is alive, so don't
  // treat it as a disconnect event.
  //
  // A disconnection means that the sibling has died. Reset the sibling's state
  // in this case.
  uint8_t buf;
  int ret = recv(sibling_state_->fd_in.get(), &buf, sizeof(buf),
                 MSG_PEEK | MSG_DONTWAIT);
  if ((ret == 0) || ((ret < 0) && (errno != EAGAIN))) {
    // Notify concierge of the sibling VM process dying for it to update its
    // internal state. `sibling_state` houses `sibling_dead_cb` so this
    // needs to happen before it's reset.
    std::move(sibling_state_->sibling_dead_cb).Run(id_);

    // The sibling VM process is dead, reset all its state. This also ensures
    // that `sibling_dead_cb` is only called once.
    sibling_state_.reset();
  }
}

void TerminaVm::SetSiblingDeadCb(
    base::OnceCallback<void(VmId vm_id)> sibling_dead_cb) {
  if (!sibling_state_) {
    LOG(ERROR) << "Sibling VM not initialized to set callback";
    return;
  }

  sibling_state_->sibling_dead_cb = std::move(sibling_dead_cb);
}

bool TerminaVm::Shutdown() {
  // Notify arc-patchpanel that the VM is down.
  // This should run before the process existence check below since we still
  // want to release the network resources on crash.
  // Note the client will only be null during testing.
  if (network_client_ &&
      !network_client_->NotifyTerminaVmShutdown(vsock_cid_)) {
    LOG(WARNING) << "Unable to notify networking services";
  }

  // Notify permission service of VM destruction.
  if (!permission_token_.empty()) {
    vm_permission::UnregisterVm(bus_, vm_permission_service_proxy_, id_);
  }

  // All parts above this are common to a regular VM as well as a sibling VM.
  if (USE_CROSVM_SIBLINGS) {
    return ShutdownSiblingVm();
  }

  // Do a check here to make sure the process is still around.  It may have
  // crashed and we don't want to be waiting around for an RPC response that's
  // never going to come.  kill with a signal value of 0 is explicitly
  // documented as a way to check for the existence of a process.
  if (!CheckProcessExists(process_.pid())) {
    // The process is already gone.
    process_.Release();
    return true;
  }

  grpc::Status status = SendVMShutdownMessage();
  // brillo::ProcessImpl doesn't provide a timed wait function and while the
  // Shutdown RPC may have been successful we can't really trust crosvm to
  // actually exit.  This may result in an untimed wait() blocking indefinitely.
  // Instead, do a timed wait here and only return success if the process
  // _actually_ exited as reported by the kernel, which is really the only
  // thing we can trust here.
  if (status.ok() && WaitForChild(process_.pid(), kChildExitTimeout)) {
    process_.Release();
    return true;
  }

  LOG(WARNING) << "Shutdown RPC failed for VM " << vsock_cid_ << " with error "
               << "code " << status.error_code() << ": "
               << status.error_message();

  // Try to shut it down via the crosvm socket.
  Stop();

  // We can't actually trust the exit codes that crosvm gives us so just see if
  // it exited.
  if (WaitForChild(process_.pid(), kChildExitTimeout)) {
    process_.Release();
    return true;
  }

  LOG(WARNING) << "Failed to stop VM " << vsock_cid_ << " via crosvm socket";

  // Kill the process with SIGTERM.
  if (process_.Kill(SIGTERM, kChildExitTimeout.InSeconds())) {
    return true;
  }

  LOG(WARNING) << "Failed to kill VM " << vsock_cid_ << " with SIGTERM";

  // Kill it with fire.
  if (process_.Kill(SIGKILL, kChildExitTimeout.InSeconds())) {
    return true;
  }

  LOG(ERROR) << "Failed to kill VM " << vsock_cid_ << " with SIGKILL";
  return false;
}

bool TerminaVm::ConfigureNetwork(const std::vector<string>& nameservers,
                                 const std::vector<string>& search_domains) {
  LOG(INFO) << "Configuring network for VM " << vsock_cid_;

  vm_tools::NetworkConfigRequest request;
  vm_tools::EmptyMessage response;

  vm_tools::IPv4Config* config = request.mutable_ipv4_config();
  config->set_address(IPv4Address());
  config->set_gateway(GatewayAddress());
  config->set_netmask(Netmask());

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->ConfigureNetwork(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to configure network for VM " << vsock_cid_ << ": "
               << status.error_message();
    return false;
  }

  return SetResolvConfig(nameservers, search_domains);
}

bool TerminaVm::ConfigureContainerGuest(const std::string& vm_token,
                                        const std::string& vm_username,
                                        std::string* out_error) {
  LOG(INFO) << "Configuring container guest for for VM " << vsock_cid_;

  vm_tools::ConfigureContainerGuestRequest request;
  vm_tools::EmptyMessage response;

  request.set_container_token(vm_token);
  request.set_vm_username(vm_username);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      stub_->ConfigureContainerGuest(&ctx, request, &response);
  if (!status.ok()) {
    *out_error = status.error_message();
    return false;
  }

  return true;
}

bool TerminaVm::Mount(string source,
                      string target,
                      string fstype,
                      uint64_t mountflags,
                      string options) {
  LOG(INFO) << "Mounting " << source << " on " << target << " inside VM "
            << vsock_cid_;

  vm_tools::MountRequest request;
  vm_tools::MountResponse response;

  request.mutable_source()->swap(source);
  request.mutable_target()->swap(target);
  request.mutable_fstype()->swap(fstype);
  request.set_mountflags(mountflags);
  request.mutable_options()->swap(options);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->Mount(&ctx, request, &response);
  if (!status.ok() || response.error() != 0) {
    LOG(ERROR) << "Failed to mount " << request.source() << " on "
               << request.target() << " inside VM " << vsock_cid_ << ": "
               << (status.ok() ? strerror(response.error())
                               : status.error_message());
    return false;
  }

  return true;
}

bool TerminaVm::StartTermina(
    std::string lxd_subnet,
    bool allow_privileged_containers,
    const google::protobuf::RepeatedField<int>& features,
    std::string* out_error,
    vm_tools::StartTerminaResponse* response) {
  DCHECK(out_error);
  DCHECK(response);

  // We record the kernel version early to ensure that no container has
  // been started and the VM can still be trusted.
  RecordKernelVersionForEnterpriseReporting();

  vm_tools::StartTerminaRequest request;

  request.set_tremplin_ipv4_address(GatewayAddress());
  request.mutable_lxd_ipv4_subnet()->swap(lxd_subnet);
  request.set_stateful_device(StatefulDevice());
  request.set_allow_privileged_containers(allow_privileged_containers);
  for (const auto feature : kEnabledTerminaFeatures) {
    request.add_feature(feature);
  }
  request.mutable_feature()->MergeFrom(features);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kStartTerminaTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->StartTermina(&ctx, request, response);

  if (!status.ok()) {
    LOG(ERROR) << "Failed to start Termina: " << status.error_message();
    out_error->assign(status.error_message());
    return false;
  }

  return true;
}

void TerminaVm::RecordKernelVersionForEnterpriseReporting() {
  grpc::ClientContext ctx_get_kernel_version;
  ctx_get_kernel_version.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kStartTerminaTimeoutSeconds, GPR_TIMESPAN)));
  vm_tools::EmptyMessage empty;
  vm_tools::GetKernelVersionResponse grpc_response;
  grpc::Status get_kernel_version_status =
      stub_->GetKernelVersion(&ctx_get_kernel_version, empty, &grpc_response);
  if (!get_kernel_version_status.ok()) {
    LOG(WARNING) << "Failed to retrieve kernel version for VM " << vsock_cid_
                 << ": " << get_kernel_version_status.error_message();
  } else {
    kernel_version_ =
        grpc_response.kernel_release() + " " + grpc_response.kernel_version();
  }
}

bool TerminaVm::AttachUsbDevice(uint8_t bus,
                                uint8_t addr,
                                uint16_t vid,
                                uint16_t pid,
                                int fd,
                                uint8_t* out_port) {
  return vm_tools::concierge::AttachUsbDevice(GetVmSocketPath(), bus, addr, vid,
                                              pid, fd, out_port);
}

bool TerminaVm::DetachUsbDevice(uint8_t port) {
  return vm_tools::concierge::DetachUsbDevice(GetVmSocketPath(), port);
}

bool TerminaVm::ListUsbDevice(std::vector<UsbDeviceEntry>* device) {
  return vm_tools::concierge::ListUsbDevice(GetVmSocketPath(), device);
}

void TerminaVm::HandleSuspendImminent() {
  LOG(INFO) << "Preparing to suspend";

  vm_tools::EmptyMessage request;
  vm_tools::EmptyMessage response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->PrepareToSuspend(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to prepare for suspending" << status.error_message();
  }

  Suspend();
}

void TerminaVm::HandleSuspendDone() {
  Resume();
}

bool TerminaVm::Mount9P(uint32_t port, string target) {
  LOG(INFO) << "Mounting 9P file system from port " << port << " on " << target;

  vm_tools::Mount9PRequest request;
  vm_tools::MountResponse response;

  request.set_port(port);
  request.set_target(std::move(target));

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->Mount9P(&ctx, request, &response);
  if (!status.ok() || response.error() != 0) {
    LOG(ERROR) << "Failed to mount 9P server on " << request.target()
               << " inside VM " << vsock_cid_ << ": "
               << (status.ok() ? strerror(response.error())
                               : status.error_message());
    return false;
  }

  return true;
}

bool TerminaVm::MountExternalDisk(string source, std::string target_dir) {
  const string target = "/mnt/external/" + target_dir;

  LOG(INFO) << "Mounting an external disk on " << target;

  vm_tools::MountRequest request;
  vm_tools::MountResponse response;

  request.set_source(std::move(source));
  request.set_target(std::move(target));
  request.set_fstype("btrfs");
  request.set_options("");
  request.set_create_target(true);
  request.set_permissions(0777);
  request.set_mkfs_if_needed(true);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->Mount(&ctx, request, &response);
  if (!status.ok() || response.error() != 0) {
    LOG(ERROR) << "Failed to mount an external disk " << request.source()
               << " on " << request.target() << " inside VM " << vsock_cid_
               << ": "
               << (status.ok() ? strerror(response.error())
                               : status.error_message());
    return false;
  }

  return true;
}

bool TerminaVm::SetResolvConfig(const std::vector<string>& nameservers,
                                const std::vector<string>& search_domains) {
  LOG(INFO) << "Setting resolv config for VM " << vsock_cid_;

  vm_tools::SetResolvConfigRequest request;
  vm_tools::EmptyMessage response;

  vm_tools::ResolvConfig* resolv_config = request.mutable_resolv_config();

  google::protobuf::RepeatedPtrField<string> request_nameservers(
      nameservers.begin(), nameservers.end());
  resolv_config->mutable_nameservers()->Swap(&request_nameservers);

  google::protobuf::RepeatedPtrField<string> request_search_domains(
      search_domains.begin(), search_domains.end());
  resolv_config->mutable_search_domains()->Swap(&request_search_domains);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->SetResolvConfig(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to set resolv config for VM " << vsock_cid_ << ": "
               << status.error_message();
    return false;
  }

  return true;
}

void TerminaVm::HostNetworkChanged() {
  LOG(INFO) << "Sending OnHostNetworkChanged for VM " << vsock_cid_;

  vm_tools::EmptyMessage request;
  vm_tools::EmptyMessage response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->OnHostNetworkChanged(&ctx, request, &response);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to send OnHostNetworkChanged for VM " << vsock_cid_
                 << ": " << status.error_message();
  }
}

bool TerminaVm::SetTime(string* failure_reason) {
  DCHECK(failure_reason);

  base::Time now = base::Time::Now();
  struct timeval current = now.ToTimeVal();

  vm_tools::SetTimeRequest request;
  vm_tools::EmptyMessage response;

  google::protobuf::Timestamp* timestamp = request.mutable_time();
  timestamp->set_seconds(current.tv_sec);
  timestamp->set_nanos(current.tv_usec * 1000);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->SetTime(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to set guest time on VM " << vsock_cid_ << ":"
               << status.error_message();

    *failure_reason = status.error_message();
    return false;
  }
  return true;
}

bool TerminaVm::GetVmEnterpriseReportingInfo(
    GetVmEnterpriseReportingInfoResponse* response) {
  LOG(INFO) << "Get enterprise reporting info";
  if (kernel_version_.empty()) {
    response->set_success(false);
    response->set_failure_reason(
        "Kernel version could not be recorded at startup.");
    return false;
  }

  response->set_success(true);
  response->set_vm_kernel_version(kernel_version_);
  return true;
}

// static
bool TerminaVm::SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state) {
  return VmBaseImpl::SetVmCpuRestriction(cpu_restriction_state,
                                         kTerminaCpuCgroup) &&
         VmBaseImpl::SetVmCpuRestriction(cpu_restriction_state,
                                         kTerminaVcpuCpuCgroup);
}

// Extract the disk index of a virtio-blk device name.
// |name| should match "/dev/vdX", where X is in the range 'a' to 'z'.
// Returns the zero-based index of the disk (e.g. 'a' = 0, 'b' = 1, etc.).
static int DiskIndexFromName(const std::string& name) {
  // TODO(dverkamp): handle more than 26 disks? (e.g. /dev/vdaa)
  if (name.length() != 8) {
    return kInvalidDiskIndex;
  }

  int disk_letter = name[7];
  if (disk_letter < 'a' || disk_letter > 'z') {
    return kInvalidDiskIndex;
  }

  return disk_letter - 'a';
}

bool TerminaVm::ResizeDiskImage(uint64_t new_size) {
  auto disk_index = DiskIndexFromName(stateful_device_);
  if (disk_index == kInvalidDiskIndex) {
    LOG(ERROR) << "Could not determine disk index from stateful device name "
               << stateful_device_;
    return false;
  }
  return CrosvmDiskResize(GetVmSocketPath(), disk_index, new_size);
}

bool TerminaVm::ResizeFilesystem(uint64_t new_size) {
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::ResizeFilesystemRequest request;
  vm_tools::ResizeFilesystemResponse response;
  request.set_size(new_size);
  grpc::Status status = stub_->ResizeFilesystem(&ctx, request, &response);
  if (status.ok())
    return true;
  LOG(ERROR) << "Resize filesystem failed (" << status.error_code()
             << "): " << status.error_message();
  return false;
}

vm_tools::concierge::DiskImageStatus TerminaVm::ResizeDisk(
    uint64_t new_size, std::string* failure_reason) {
  if (stateful_resize_type_ != DiskResizeType::NONE) {
    LOG(ERROR) << "Attempted resize while resize is already in progress";
    *failure_reason = "Resize already in progress";
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
    return last_stateful_resize_status_;
  }

  LOG(INFO) << "TerminaVm resize request: current size = " << stateful_size_
            << " new size = " << new_size;

  if (new_size == stateful_size_) {
    LOG(INFO) << "Disk is already requested size";
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_RESIZED;
    return last_stateful_resize_status_;
  }

  stateful_target_size_ = new_size;

  if (new_size > stateful_size_) {
    LOG(INFO) << "Expanding disk";
    // Expand disk image first, then expand filesystem.
    if (!ResizeDiskImage(new_size)) {
      LOG(ERROR) << "ResizeDiskImage failed";
      *failure_reason = "ResizeDiskImage failed";
      last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
      return last_stateful_resize_status_;
    }

    if (!ResizeFilesystem(new_size)) {
      LOG(ERROR) << "ResizeFilesystem failed";
      *failure_reason = "ResizeFilesystem failed";
      last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
      return last_stateful_resize_status_;
    }

    LOG(INFO) << "ResizeFilesystem in progress";
    stateful_resize_type_ = DiskResizeType::EXPAND;
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_IN_PROGRESS;
    return last_stateful_resize_status_;
  } else {
    DCHECK(new_size < stateful_size_);

    LOG(INFO) << "Shrinking disk";

    // Shrink filesystem first, then shrink disk image.
    if (!ResizeFilesystem(new_size)) {
      LOG(ERROR) << "ResizeFilesystem failed";
      *failure_reason = "ResizeFilesystem failed";
      last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
      return last_stateful_resize_status_;
    }

    LOG(INFO) << "ResizeFilesystem in progress";
    stateful_resize_type_ = DiskResizeType::SHRINK;
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_IN_PROGRESS;
    return last_stateful_resize_status_;
  }
}

vm_tools::concierge::DiskImageStatus TerminaVm::GetDiskResizeStatus(
    std::string* failure_reason) {
  if (stateful_resize_type_ == DiskResizeType::NONE) {
    return last_stateful_resize_status_;
  }

  // If a resize is in progress, then we must be waiting on filesystem resize to
  // complete. Check its status and update our state to match.
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::EmptyMessage request;
  vm_tools::GetResizeStatusResponse response;

  grpc::Status status = stub_->GetResizeStatus(&ctx, request, &response);

  if (!status.ok()) {
    stateful_resize_type_ = DiskResizeType::NONE;
    LOG(ERROR) << "GetResizeStatus RPC failed";
    *failure_reason = "GetResizeStatus RPC failed";
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
    return last_stateful_resize_status_;
  }

  if (response.resize_in_progress()) {
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_IN_PROGRESS;
    return last_stateful_resize_status_;
  }

  if (response.current_size() != stateful_target_size_) {
    stateful_resize_type_ = DiskResizeType::NONE;
    LOG(ERROR) << "Unexpected size after filesystem resize: got "
               << response.current_size() << ", expected "
               << stateful_target_size_;
    *failure_reason = "Unexpected size after filesystem resize";
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
    return last_stateful_resize_status_;
  }

  stateful_size_ = response.current_size();

  if (stateful_resize_type_ == DiskResizeType::SHRINK) {
    LOG(INFO) << "Filesystem shrink complete; shrinking disk image";
    if (!ResizeDiskImage(response.current_size())) {
      LOG(ERROR) << "ResizeDiskImage failed";
      *failure_reason = "ResizeDiskImage failed";
      last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
      return last_stateful_resize_status_;
    }
  } else {
    LOG(INFO) << "Filesystem expansion complete";
  }

  LOG(INFO) << "Disk resize successful";
  stateful_resize_type_ = DiskResizeType::NONE;
  last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_RESIZED;
  return last_stateful_resize_status_;
}

uint64_t TerminaVm::GetMinDiskSize() {
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::EmptyMessage request;
  vm_tools::GetResizeBoundsResponse response;

  grpc::Status status = stub_->GetResizeBounds(&ctx, request, &response);

  if (!status.ok()) {
    LOG(ERROR) << "GetResizeBounds RPC failed";
    return 0;
  }

  return response.minimum_size();
}

uint64_t TerminaVm::GetAvailableDiskSpace() {
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::EmptyMessage request;
  vm_tools::GetAvailableSpaceResponse response;

  grpc::Status status = stub_->GetAvailableSpace(&ctx, request, &response);

  if (!status.ok()) {
    LOG(ERROR) << "GetAvailableSpace RPC failed";
    return 0;
  }

  return response.available_space();
}

// Helper function to convert spaced enum to vm_tools equivalent.
vm_tools::StatefulDiskSpaceState MapSpacedStateToGuestState(
    spaced::StatefulDiskSpaceState state) {
  switch (state) {
    case spaced::StatefulDiskSpaceState::NORMAL:
      return vm_tools::StatefulDiskSpaceState::DISK_NORMAL;
      break;
    case spaced::StatefulDiskSpaceState::LOW:
      return vm_tools::StatefulDiskSpaceState::DISK_LOW;
      break;
    case spaced::StatefulDiskSpaceState::CRITICAL:
      return vm_tools::StatefulDiskSpaceState::DISK_CRITICAL;
      break;
    case spaced::StatefulDiskSpaceState::NONE:
    default:
      return vm_tools::StatefulDiskSpaceState::DISK_NONE;
  }
}

void TerminaVm::HandleStatefulUpdate(
    const spaced::StatefulDiskSpaceUpdate update) {
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::UpdateStorageBalloonRequest request;
  vm_tools::UpdateStorageBalloonRequest out;
  request.set_state(MapSpacedStateToGuestState(update.state()));
  request.set_free_space_bytes(update.free_space_bytes());
  vm_tools::UpdateStorageBalloonResponse response;

  grpc::Status status = stub_->UpdateStorageBalloon(&ctx, request, &response);

  if (!status.ok()) {
    LOG(ERROR) << "HandleStatefulUpdate RPC failed";
  }

  return;
}

uint32_t TerminaVm::GatewayAddress() const {
  return subnet_->AddressAtOffset(kHostAddressOffset);
}

uint32_t TerminaVm::IPv4Address() const {
  return subnet_->AddressAtOffset(kGuestAddressOffset);
}

uint32_t TerminaVm::Netmask() const {
  return subnet_->Netmask();
}

uint32_t TerminaVm::ContainerNetmask() const {
  if (container_subnet_)
    return container_subnet_->Netmask();

  return INADDR_ANY;
}

size_t TerminaVm::ContainerPrefixLength() const {
  if (container_subnet_)
    return container_subnet_->PrefixLength();

  return 0;
}

uint32_t TerminaVm::ContainerSubnet() const {
  if (container_subnet_)
    return container_subnet_->AddressAtOffset(0);

  return INADDR_ANY;
}

std::string TerminaVm::PermissionToken() const {
  return permission_token_;
}

VmInterface::Info TerminaVm::GetInfo() {
  VmInterface::Info info = {
      .ipv4_address = IPv4Address(),
      .pid = pid(),
      .cid = cid(),
      .vm_memory_id = vm_memory_id_,
      .seneschal_server_handle = seneschal_server_handle(),
      .permission_token = permission_token_,
      .status = IsTremplinStarted() ? VmInterface::Status::RUNNING
                                    : VmInterface::Status::STARTING,
      .type = classification_,
  };

  return info;
}

void TerminaVm::set_kernel_version_for_testing(std::string kernel_version) {
  kernel_version_ = kernel_version;
}

void TerminaVm::set_stub_for_testing(
    std::unique_ptr<vm_tools::Maitred::Stub> stub) {
  stub_ = std::move(stub);
}

std::unique_ptr<TerminaVm> TerminaVm::CreateForTesting(
    std::unique_ptr<patchpanel::Subnet> subnet,
    uint32_t vsock_cid,
    base::FilePath runtime_dir,
    base::FilePath log_path,
    std::string stateful_device,
    uint64_t stateful_size,
    int64_t mem_mib,
    std::string kernel_version,
    std::unique_ptr<vm_tools::Maitred::Stub> stub,
    VmInfo::VmType classification,
    VmBuilder vm_builder,
    base::Thread* dbus_thread) {
  VmFeatures features{
      .gpu = false,
      .software_tpm = false,
      .vtpm_proxy = false,
      .audio_capture = false,
  };
  auto vm = base::WrapUnique(new TerminaVm(
      std::move(subnet), vsock_cid, nullptr, std::move(runtime_dir),
      std::move(log_path), std::move(stateful_device), std::move(stateful_size),
      mem_mib, features, classification, dbus_thread));
  vm->set_kernel_version_for_testing(kernel_version);
  vm->set_stub_for_testing(std::move(stub));

  return vm;
}

}  // namespace concierge
}  // namespace vm_tools
