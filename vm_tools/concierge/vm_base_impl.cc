// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_base_impl.h"

#include <sys/wait.h>

#include <optional>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/task/sequenced_task_runner.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/concierge/crosvm_control.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge {
namespace {

// How long to wait between checking if the VM process has exited.
constexpr base::TimeDelta kExitCheckRepeatDelay = base::Milliseconds(250);

}  // namespace

VmBaseImpl::VmBaseImpl(Config config)
    : network_client_(std::move(config.network_client)),
      seneschal_server_proxy_(std::move(config.seneschal_server_proxy)),
      vsock_cid_(config.vsock_cid),
      control_socket_path_(
          config.runtime_dir.Append(config.cros_vm_socket).value()),
      weak_ptr_factory_(this) {
  // Take ownership of the runtime directory.
  CHECK(base::DirectoryExists(config.runtime_dir));
  CHECK(runtime_dir_.Set(config.runtime_dir));
}

VmBaseImpl::~VmBaseImpl() {
  CHECK(!IsRunning());
}

void VmBaseImpl::PerformStopSequence(
    StopType type, base::OnceCallback<void(StopResult)> callback) {
  // Nothing is running.
  if (process_.pid() == 0) {
    std::move(callback).Run(StopResult::SUCCESS);
    return;
  }

  // If the VM is currently stopping a new sequence cannot be started.
  if (IsStopping()) {
    std::move(callback).Run(StopResult::STOPPING);
    return;
  }

  stop_complete_callback_ = std::move(callback);

  // Get the list of tasks to be performed to stop the VM.
  stop_steps_ = GetStopSteps(type);

  // Send -1 as the previous step to indicate that this is the beginning of a
  // new stop sequence.
  PerformStopSequenceInternal(type, stop_steps_.begin());
}

void VmBaseImpl::OnStopSequenceComplete(StopResult result) {
  stop_steps_.clear();

  // If the VM was stopped, callbacks may destroy this instance.
  // Post a task to run the callback outside the scope of this
  // instance.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(stop_complete_callback_), result));
}

void VmBaseImpl::PerformStopSequenceInternal(
    StopType type, std::vector<StopStep>::iterator next_step) {
  // No more steps and the VM is still alive. This stop sequence failed.
  if (next_step == stop_steps_.end()) {
    OnStopSequenceComplete(StopResult::FAILURE);
  }

  // If the VM doesn't stop after this step, run PerformStopSequenceInternal
  // again to move on to the next step.
  base::OnceClosure timeout_callback =
      base::BindOnce(&VmBaseImpl::PerformStopSequenceInternal,
                     weak_ptr_factory_.GetWeakPtr(), type, next_step + 1);

  // Run the step and then check if the process exits. After completion,
  // the stop sequence will either be finished or move on to the next step.
  std::move(next_step->task)
      .Run(base::BindOnce(&VmBaseImpl::CheckForExit,
                          weak_ptr_factory_.GetWeakPtr(),
                          base::TimeTicks::Now() + next_step->exit_timeout,
                          std::move(timeout_callback)));
}

void VmBaseImpl::CheckForExit(base::TimeTicks deadline,
                              base::OnceClosure timeout_callback) {
  if (!IsRunning()) {
    LOG(INFO) << "VM: " << vsock_cid_ << " stopped successfully";
    process_.Release();
    OnStopSequenceComplete(StopResult::SUCCESS);
    return;
  }

  // Timed out waiting for the VM to exit. Stop checking.
  if (base::TimeTicks::Now() > deadline) {
    std::move(timeout_callback).Run();
    return;
  }

  // The VM is still alive. Check again in the near future and forward the
  // timeout callback to the next check.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&VmBaseImpl::CheckForExit, weak_ptr_factory_.GetWeakPtr(),
                     deadline, std::move(timeout_callback)),
      kExitCheckRepeatDelay);
}

bool VmBaseImpl::IsRunning() {
  pid_t ret = HANDLE_EINTR(waitpid(process_.pid(), nullptr, WNOHANG));

  // ret == 0 means that the child is still alive

  // The VM process exited.
  if (ret == process_.pid() || (ret < 0 && errno == ECHILD)) {
    return false;
  }

  if (ret < 0) {
    PLOG(ERROR) << "Failed to wait for child process";
  }

  return true;
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
  bool result = CrosvmControl::Get()->SetBalloonSize(GetVmSocketPath(),
                                                     byte_size, std::nullopt);
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

void VmBaseImpl::StopViaCrosvm(base::OnceClosure callback) {
  // Stop via the crosvm control socket
  CrosvmControl::Get()->StopVm(GetVmSocketPath());
  std::move(callback).Run();
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

void VmBaseImpl::KillVmProcess(int signal, base::OnceClosure callback) {
  // Use a timeout of 0 to not block until the process exits.
  process_.Kill(signal, 0);

  std::move(callback).Run();
}

}  // namespace vm_tools::concierge
