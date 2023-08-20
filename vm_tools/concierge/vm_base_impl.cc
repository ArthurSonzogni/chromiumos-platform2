// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_base_impl.h"

#include <optional>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/concierge/crosvm_control.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge {

VmBaseImpl::VmBaseImpl(Config config)
    : network_client_(std::move(config.network_client)),
      seneschal_server_proxy_(std::move(config.seneschal_server_proxy)),
      vsock_cid_(config.vsock_cid),
      control_socket_path_(
          config.runtime_dir.Append(config.cros_vm_socket).value()) {
  // Take ownership of the runtime directory.
  CHECK(base::DirectoryExists(config.runtime_dir));
  CHECK(runtime_dir_.Set(config.runtime_dir));
}

std::optional<BalloonStats> VmBaseImpl::GetBalloonStats(
    std::optional<base::TimeDelta> timeout) {
  return vm_tools::concierge::GetBalloonStats(GetVmSocketPath(), timeout);
}

std::optional<BalloonWorkingSet> VmBaseImpl::GetBalloonWorkingSet() {
  return vm_tools::concierge::GetBalloonWorkingSet(GetVmSocketPath());
}

bool VmBaseImpl::SetBalloonSize(int64_t byte_size) {
  if (byte_size < 0) {
    LOG(ERROR) << "Skipping setting a negative balloon size: " << byte_size;
  }
  bool result =
      CrosvmControl::Get()->SetBalloonSize(GetVmSocketPath(), byte_size);
  if (result && balloon_policy_) {
    balloon_policy_->UpdateCurrentBalloonSize(byte_size);
  }
  return result;
}

bool VmBaseImpl::SetBalloonWorkingSetConfig(const BalloonWSRConfigFfi* config) {
  return CrosvmControl::Get()->SetBalloonWorkingSetConfig(GetVmSocketPath(),
                                                          config);
}

const std::unique_ptr<BalloonPolicyInterface>& VmBaseImpl::GetBalloonPolicy(
    const MemoryMargins& margins, const std::string& vm) {
  if (!balloon_policy_) {
    balloon_policy_ = std::make_unique<BalanceAvailableBalloonPolicy>(
        margins.critical, 0, vm);
  }
  return balloon_policy_;
}

bool VmBaseImpl::AttachUsbDevice(uint8_t bus,
                                 uint8_t addr,
                                 uint16_t vid,
                                 uint16_t pid,
                                 int fd,
                                 uint8_t* out_port) {
  return vm_tools::concierge::AttachUsbDevice(GetVmSocketPath(), bus, addr, vid,
                                              pid, fd, out_port);
}

bool VmBaseImpl::DetachUsbDevice(uint8_t port) {
  return vm_tools::concierge::DetachUsbDevice(GetVmSocketPath(), port);
}

bool VmBaseImpl::ListUsbDevice(std::vector<UsbDeviceEntry>* devices) {
  return vm_tools::concierge::ListUsbDevice(GetVmSocketPath(), devices);
}

// static
bool VmBaseImpl::SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state,
                                     const char* cpu_cgroup) {
  int cpu_shares = 1024;  // TODO(sonnyrao): Adjust |cpu_shares|.
  switch (cpu_restriction_state) {
    case CPU_RESTRICTION_FOREGROUND:
      break;
    case CPU_RESTRICTION_BACKGROUND:
    case CPU_RESTRICTION_BACKGROUND_WITH_CFS_QUOTA_ENFORCED:
      cpu_shares = 64;
      break;
    default:
      NOTREACHED();
  }
  return UpdateCpuShares(base::FilePath(cpu_cgroup), cpu_shares);
}

// static
void VmBaseImpl::RunFailureAggressiveBalloonCallback(
    AggressiveBalloonCallback callback, std::string failure_reason) {
  AggressiveBalloonResponse response;
  response.set_success(false);
  response.set_failure_reason(failure_reason);
  std::move(callback).Run(response);
}

bool VmBaseImpl::StartProcess(base::StringPairs args) {
  std::string command_line_for_log{};

  for (std::pair<std::string, std::string>& arg : args) {
    command_line_for_log += arg.first;
    command_line_for_log += " ";

    process_.AddArg(std::move(arg.first));
    if (!arg.second.empty()) {
      command_line_for_log += arg.second;
      command_line_for_log += " ";
      process_.AddArg(std::move(arg.second));
    }
  }
  LOG(INFO) << "Invoking VM: " << command_line_for_log;
  if (!process_.Start()) {
    PLOG(ERROR) << "Failed to start VM process";
    return false;
  }

  return true;
}

const std::string& VmBaseImpl::GetVmSocketPath() const {
  return control_socket_path_;
}

bool VmBaseImpl::Stop() const {
  return CrosvmControl::Get()->StopVm(GetVmSocketPath());
}

bool VmBaseImpl::SuspendCrosvm() const {
  return CrosvmControl::Get()->SuspendVm(GetVmSocketPath());
}

bool VmBaseImpl::ResumeCrosvm() const {
  return CrosvmControl::Get()->ResumeVm(GetVmSocketPath());
}

uint32_t VmBaseImpl::seneschal_server_handle() const {
  if (seneschal_server_proxy_)
    return seneschal_server_proxy_->handle();

  return 0;
}

void VmBaseImpl::MakeRtVcpu() {
  CrosvmControl::Get()->MakeRtVm(GetVmSocketPath());
}

void VmBaseImpl::HandleSwapVmRequest(const SwapVmRequest& request,
                                     SwapVmCallback callback) {
  SwapVmResponse response;
  response.set_success(false);
  response.set_failure_reason("vmm-swap is not supported on this vm");
  std::move(callback).Run(response);
}

void VmBaseImpl::InflateAggressiveBalloon(AggressiveBalloonCallback callback) {
  RunFailureAggressiveBalloonCallback(std::move(callback),
                                      "Unsupported by target VM");
}

void VmBaseImpl::StopAggressiveBalloon(AggressiveBalloonResponse& response) {
  response.set_success(false);
  response.set_failure_reason("Unsupported by target VM");
}

}  // namespace vm_tools::concierge
