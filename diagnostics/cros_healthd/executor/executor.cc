// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor.h"

#include <bits/types/siginfo_t.h>
#include <inttypes.h>
#include <linux/capability.h>
#include <sys/capability.h>
#include <sys/types.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <re2/re2.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/executor/utils/delegate_process.h"
#include "diagnostics/cros_healthd/executor/utils/process_control.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/process/process_with_output.h"
#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

namespace seccomp_file {

// SECCOMP policy for evdev related routines.
constexpr char kEvdev[] = "evdev-seccomp.policy";
// SECCOMP policy for ectool pwmgetfanrpm.
constexpr char kFanSpeed[] = "ectool_pwmgetfanrpm-seccomp.policy";
// SECCOMP policy for fingerprint related routines.
constexpr char kFingerprint[] = "fingerprint-seccomp.policy";
// SECCOMP policy for hciconfig.
constexpr char kHciconfig[] = "hciconfig-seccomp.policy";
// SECCOMP policy for IW related routines.
constexpr char kIw[] = "iw-seccomp.policy";
// SECCOMP policy for LED related routines.
constexpr char kLed[] = "ec_led-seccomp.policy";
// SECCOMP policy for ectool motionsense lid_angle.
constexpr char kLidAngle[] = "ectool_motionsense_lid_angle-seccomp.policy";
// SECCOMP policy for memtester.
constexpr char kMemtester[] = "memtester-seccomp.policy";

}  // namespace seccomp_file

// Amount of time we wait for a process to respond to SIGTERM before killing it.
constexpr base::TimeDelta kTerminationTimeout = base::Seconds(2);

// Null capability for delegate process.
constexpr uint64_t kNullCapability = 0;

// The user and group for accessing fingerprint.
constexpr char kFingerprintUserAndGroup[] = "healthd_fp";

// The user and group for accessing Evdev.
constexpr char kEvdevUserAndGroup[] = "healthd_evdev";

// The user and group for accessing EC.
constexpr char kEcUserAndGroup[] = "healthd_ec";

// The path to ectool binary.
constexpr char kEctoolBinary[] = "/usr/sbin/ectool";
// The ectool command used to collect fan speed in RPM.
constexpr char kGetFanRpmCommand[] = "pwmgetfanrpm";
// The ectool commands used to collect lid angle.
constexpr char kMotionSenseCommand[] = "motionsense";
constexpr char kLidAngleCommand[] = "lid_angle";

// The iw command used to collect different wireless data.
constexpr char kIwBinary[] = "/usr/sbin/iw";
constexpr char kIwInterfaceCommand[] = "dev";
constexpr char kIwInfoCommand[] = "info";
constexpr char kIwLinkCommand[] = "link";
constexpr char kIwScanCommand[] = "scan";
constexpr char kIwDumpCommand[] = "dump";
// wireless interface name start with "wl" or "ml" and end it with a number. All
// characters are in lowercase.  Max length is 16 characters.
constexpr auto kWirelessInterfaceRegex = R"(([wm]l[a-z][a-z0-9]{1,12}[0-9]))";

// The path to memtester binary.
constexpr char kMemtesterBinary[] = "/usr/sbin/memtester";

// The path to hciconfig binary.
constexpr char kHciconfigBinary[] = "/usr/bin/hciconfig";

// A read-only mount point for cros_ec.
constexpr char kCrosEcDevice[] = "/dev/cros_ec";

// Whitelist of msr registers that can be read by the ReadMsr call.
constexpr uint32_t kMsrAccessAllowList[] = {
    cpu_msr::kIA32TmeCapability, cpu_msr::kIA32TmeActivate,
    cpu_msr::kIA32FeatureControl, cpu_msr::kVmCr};

// Path to the UEFI SecureBoot file. This file can be read by root only.
// It's one of EFI globally defined variables (EFI_GLOBAL_VARIABLE, fixed UUID
// 8be4df61-93ca-11d2-aa0d-00e098032b8c)
// See also:
// https://uefi.org/sites/default/files/resources/UEFI_Spec_2_9_2021_03_18.pdf
constexpr char kUEFISecureBootVarPath[] =
    "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c";
// Path to the UEFI platform size file.
constexpr char kUEFIPlatformSizeFile[] = "/sys/firmware/efi/fw_platform_size";

// Error message when failing to launch delegate.
constexpr char kFailToLaunchDelegate[] = "Failed to launch delegate";

// Reads file and reply the result to a callback. Will reply empty string if
// cannot read the file.
void ReadRawFileAndReplyCallback(
    const base::FilePath& file,
    base::OnceCallback<void(const std::string&)> callback) {
  std::string content = "";
  LOG_IF(ERROR, !base::ReadFileToString(file, &content))
      << "Failed to read file: " << file;
  std::move(callback).Run(content);
}

// Same as above but also trim the string.
void ReadTrimFileAndReplyCallback(
    const base::FilePath& file,
    base::OnceCallback<void(const std::string&)> callback) {
  std::string content = "";
  LOG_IF(ERROR, !ReadAndTrimString(file, &content))
      << "Failed to read or trim file: " << file;
  std::move(callback).Run(content);
}

// A helper to create a delegate callback which only run once and reply the
// result to a callback. This takes a delegate instance and will destruct it
// after the callback is called. If the callback is dropped (e.g. mojo
// disconnect), the `default_args` is used to reply the callback.
//
// Example:
//   auto delegate = std::make_unique<DelegateProcess>(...);
//   // Get pointer and move the delegate into the callback.
//   auto* delegate_ptr = delegate.get();
//   delegate_ptr->remote()->SomeMethod(
//     CreateOnceDelegateCallback(
//       std::move(delegate), std::move(callback), ...));
//   delegate_ptr->StartAsync();
//
template <typename Callback, typename... Args>
Callback CreateOnceDelegateCallback(std::unique_ptr<DelegateProcess> delegate,
                                    Callback callback,
                                    Args... default_args) {
  base::OnceClosure deleter = base::DoNothingWithBoundArgs(std::move(delegate));
  return mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback).Then(std::move(deleter)), std::move(default_args)...);
}

}  // namespace

// Exported for testing.
bool IsValidWirelessInterfaceName(const std::string& interface_name) {
  return (RE2::FullMatch(interface_name, kWirelessInterfaceRegex, nullptr));
}

Executor::Executor(
    const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
    mojo::PendingReceiver<mojom::Executor> receiver,
    brillo::ProcessReaper* process_reaper,
    base::OnceClosure on_disconnect)
    : mojo_task_runner_(mojo_task_runner),
      receiver_{this /* impl */, std::move(receiver)},
      process_reaper_(process_reaper) {
  receiver_.set_disconnect_handler(std::move(on_disconnect));
}

void Executor::GetFanSpeed(GetFanSpeedCallback callback) {
  std::vector<std::string> command = {kEctoolBinary, kGetFanRpmCommand};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kFanSpeed, kEcUserAndGroup,
      CAP_TO_MASK(CAP_SYS_RAWIO),
      /*readonly_mount_points=*/
      std::vector<base::FilePath>{base::FilePath(kCrosEcDevice)},
      /*writable_mount_points=*/std::vector<base::FilePath>{});

  RunAndWaitProcess(std::move(process), std::move(callback),
                    /*combine_stdout_and_stderr=*/false);
}

void Executor::GetInterfaces(GetInterfacesCallback callback) {
  std::vector<std::string> command = {kIwBinary, kIwInterfaceCommand};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kIw, kCrosHealthdSandboxUser, kNullCapability,
      /*readonly_mount_points=*/
      std::vector<base::FilePath>{base::FilePath(kIwBinary)},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{}, NO_ENTER_NETWORK_NAMESPACE);

  RunAndWaitProcess(std::move(process), std::move(callback),
                    /*combine_stdout_and_stderr=*/false);
}

void Executor::GetLink(const std::string& interface_name,
                       GetLinkCallback callback) {
  // Sanitize against interface_name.
  if (!IsValidWirelessInterfaceName(interface_name)) {
    auto result = mojom::ExecutedProcessResult::New();
    result->err = "Illegal interface name: " + interface_name;
    result->return_code = EXIT_FAILURE;
    std::move(callback).Run(std::move(result));
    return;
  }

  std::vector<std::string> command = {kIwBinary, interface_name,
                                      kIwLinkCommand};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kIw, kCrosHealthdSandboxUser, kNullCapability,
      /*readonly_mount_points=*/
      std::vector<base::FilePath>{base::FilePath(kIwBinary)},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{}, NO_ENTER_NETWORK_NAMESPACE);

  RunAndWaitProcess(std::move(process), std::move(callback),
                    /*combine_stdout_and_stderr=*/false);
}

void Executor::GetInfo(const std::string& interface_name,
                       GetInfoCallback callback) {
  // Sanitize against interface_name.
  if (!IsValidWirelessInterfaceName(interface_name)) {
    auto result = mojom::ExecutedProcessResult::New();
    result->err = "Illegal interface name: " + interface_name;
    result->return_code = EXIT_FAILURE;
    std::move(callback).Run(std::move(result));
    return;
  }

  std::vector<std::string> command = {kIwBinary, interface_name,
                                      kIwInfoCommand};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kIw, kCrosHealthdSandboxUser, kNullCapability,
      /*readonly_mount_points=*/
      std::vector<base::FilePath>{base::FilePath(kIwBinary)},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{}, NO_ENTER_NETWORK_NAMESPACE);

  RunAndWaitProcess(std::move(process), std::move(callback),
                    /*combine_stdout_and_stderr=*/false);
}

void Executor::GetScanDump(const std::string& interface_name,
                           GetScanDumpCallback callback) {
  // Sanitize against interface_name.
  if (!IsValidWirelessInterfaceName(interface_name)) {
    auto result = mojom::ExecutedProcessResult::New();
    result->err = "Illegal interface name: " + interface_name;
    result->return_code = EXIT_FAILURE;
    std::move(callback).Run(std::move(result));
    return;
  }

  std::vector<std::string> command = {kIwBinary, interface_name, kIwScanCommand,
                                      kIwDumpCommand};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kIw, kCrosHealthdSandboxUser, kNullCapability,
      /*readonly_mount_points=*/
      std::vector<base::FilePath>{base::FilePath(kIwBinary)},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{}, NO_ENTER_NETWORK_NAMESPACE);

  RunAndWaitProcess(std::move(process), std::move(callback),
                    /*combine_stdout_and_stderr=*/false);
}

void Executor::RunMemtester(uint32_t test_mem_kib,
                            RunMemtesterCallback callback) {
  // Run with test_mem_kib memory and run for one loop.
  std::vector<std::string> command = {
      kMemtesterBinary, base::StringPrintf("%uK", test_mem_kib), "1"};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kMemtester, kCrosHealthdSandboxUser,
      CAP_TO_MASK(CAP_IPC_LOCK),
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/std::vector<base::FilePath>{});

  RunTrackedBinary(std::move(process), std::move(callback), kMemtesterBinary);
}

void Executor::RunMemtesterV2(
    uint32_t test_mem_kib,
    mojo::PendingReceiver<mojom::ProcessControl> receiver) {
  // Run with test_mem_kib memory and run for 1 loop.
  std::vector<std::string> command = {
      kMemtesterBinary, base::StringPrintf("%uK", test_mem_kib), "1"};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kMemtester, kCrosHealthdSandboxUser,
      CAP_TO_MASK(CAP_IPC_LOCK),
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/std::vector<base::FilePath>{});

  RunLongRunningProcess(std::move(process), std::move(receiver),
                        /*combine_stdout_and_stderr=*/true);
}

void Executor::KillMemtester() {
  base::AutoLock auto_lock(lock_);
  auto itr = tracked_processes_.find(kMemtesterBinary);
  if (itr == tracked_processes_.end())
    return;

  brillo::Process* process = itr->second.get();
  // If the process has ended, don't try to kill anything.
  if (!process->pid())
    return;

  // Try to terminate the process nicely, then kill it if necessary.
  if (!process->Kill(SIGTERM, kTerminationTimeout.InSeconds()))
    process->Kill(SIGKILL, kTerminationTimeout.InSeconds());
}

void Executor::GetProcessIOContents(const std::vector<uint32_t>& pids,
                                    GetProcessIOContentsCallback callback) {
  std::vector<std::pair<uint32_t, std::string>> results;

  for (const auto& pid : pids) {
    std::string result;
    if (ReadAndTrimString(base::FilePath("/proc/")
                              .Append(base::StringPrintf("%" PRId32, pid))
                              .AppendASCII("io"),
                          &result)) {
      results.push_back({pid, result});
    }
  }

  std::move(callback).Run(
      base::flat_map<uint32_t, std::string>{std::move(results)});
}

void Executor::ReadMsr(const uint32_t msr_reg,
                       uint32_t cpu_index,
                       ReadMsrCallback callback) {
  if (std::find(std::begin(kMsrAccessAllowList), std::end(kMsrAccessAllowList),
                msr_reg) == std::end(kMsrAccessAllowList)) {
    LOG(ERROR) << "MSR access not allowed";
    std::move(callback).Run(nullptr);
    return;
  }
  base::FilePath msr_path = base::FilePath("/dev/cpu")
                                .Append(base::NumberToString(cpu_index))
                                .Append("msr");
  base::File msr_fd(msr_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!msr_fd.IsValid()) {
    LOG(ERROR) << "Could not open " << msr_path.value();
    std::move(callback).Run(nullptr);
    return;
  }
  uint64_t val = 0;
  // Read MSR register. See
  // https://github.com/intel/msr-tools/blob/0fcbda4e47a2aab73904e19b3fc0a7a73135c415/rdmsr.c#L235
  // for the use of reinterpret_case
  if (sizeof(val) !=
      msr_fd.Read(msr_reg, reinterpret_cast<char*>(&val), sizeof(val))) {
    LOG(ERROR) << "Could not read MSR register from " << msr_path.value();
    std::move(callback).Run(nullptr);
    return;
  }
  std::move(callback).Run(mojom::NullableUint64::New(val));
}

void Executor::GetUEFISecureBootContent(
    GetUEFISecureBootContentCallback callback) {
  ReadRawFileAndReplyCallback(base::FilePath(kUEFISecureBootVarPath),
                              std::move(callback));
}

void Executor::GetUEFIPlatformSizeContent(
    GetUEFIPlatformSizeContentCallback callback) {
  ReadTrimFileAndReplyCallback(base::FilePath{kUEFIPlatformSizeFile},
                               std::move(callback));
}

void Executor::GetLidAngle(GetLidAngleCallback callback) {
  std::vector<std::string> command = {kEctoolBinary, kMotionSenseCommand,
                                      kLidAngleCommand};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kLidAngle, kEcUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/
      std::vector<base::FilePath>{base::FilePath(kCrosEcDevice)},
      /*writable_mount_points=*/std::vector<base::FilePath>{});

  RunAndWaitProcess(std::move(process), std::move(callback),
                    /*combine_stdout_and_stderr=*/false);
}

void Executor::GetFingerprintFrame(mojom::FingerprintCaptureType type,
                                   GetFingerprintFrameCallback callback) {
  auto delegate = std::make_unique<DelegateProcess>(
      seccomp_file::kFingerprint, kFingerprintUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{base::FilePath{fingerprint::kCrosFpPath}});

  auto* delegate_ptr = delegate.get();
  delegate_ptr->remote()->GetFingerprintFrame(
      type, CreateOnceDelegateCallback(std::move(delegate), std::move(callback),
                                       mojom::FingerprintFrameResult::New(),
                                       kFailToLaunchDelegate));
  delegate_ptr->StartAsync();
}

void Executor::GetFingerprintInfo(GetFingerprintInfoCallback callback) {
  auto delegate = std::make_unique<DelegateProcess>(
      seccomp_file::kFingerprint, kFingerprintUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{base::FilePath{fingerprint::kCrosFpPath}});

  auto* delegate_ptr = delegate.get();
  delegate_ptr->remote()->GetFingerprintInfo(CreateOnceDelegateCallback(
      std::move(delegate), std::move(callback),
      mojom::FingerprintInfoResult::New(), kFailToLaunchDelegate));
  delegate_ptr->StartAsync();
}

void Executor::SetLedColor(mojom::LedName name,
                           mojom::LedColor color,
                           SetLedColorCallback callback) {
  auto delegate = std::make_unique<DelegateProcess>(
      seccomp_file::kLed, kEcUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{base::FilePath{kCrosEcDevice}});

  auto* delegate_ptr = delegate.get();
  delegate_ptr->remote()->SetLedColor(
      name, color,
      CreateOnceDelegateCallback(std::move(delegate), std::move(callback),
                                 kFailToLaunchDelegate));
  delegate_ptr->StartAsync();
}

void Executor::ResetLedColor(ash::cros_healthd::mojom::LedName name,
                             ResetLedColorCallback callback) {
  auto delegate = std::make_unique<DelegateProcess>(
      seccomp_file::kLed, kEcUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{base::FilePath{kCrosEcDevice}});

  auto* delegate_ptr = delegate.get();
  delegate_ptr->remote()->ResetLedColor(
      name, CreateOnceDelegateCallback(std::move(delegate), std::move(callback),
                                       kFailToLaunchDelegate));
  delegate_ptr->StartAsync();
}

void Executor::GetHciDeviceConfig(GetHciDeviceConfigCallback callback) {
  std::vector<std::string> command = {kHciconfigBinary, "hci0"};
  auto process = std::make_unique<SandboxedProcess>(
      command, seccomp_file::kHciconfig, kCrosHealthdSandboxUser,
      kNullCapability,
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/std::vector<base::FilePath>{},
      NO_ENTER_NETWORK_NAMESPACE);

  RunAndWaitProcess(std::move(process), std::move(callback),
                    /*combine_stdout_and_stderr=*/false);
}

void Executor::MonitorAudioJack(
    mojo::PendingRemote<mojom::AudioJackObserver> observer,
    mojo::PendingReceiver<mojom::ProcessControl> process_control_receiver) {
  auto delegate = std::make_unique<DelegateProcess>(
      seccomp_file::kEvdev, kEvdevUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/
      std::vector<base::FilePath>{base::FilePath{"/dev/input"}},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{});

  delegate->remote()->MonitorAudioJack(std::move(observer));
  auto controller = std::make_unique<ProcessControl>(std::move(delegate));

  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&Executor::RunLongRunningDelegate,
                     weak_factory_.GetWeakPtr(), std::move(controller),
                     std::move(process_control_receiver)));
}

void Executor::MonitorTouchpad(
    mojo::PendingRemote<mojom::TouchpadObserver> observer,
    mojo::PendingReceiver<mojom::ProcessControl> process_control_receiver) {
  auto delegate = std::make_unique<DelegateProcess>(
      seccomp_file::kEvdev, kEvdevUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/
      std::vector<base::FilePath>{base::FilePath{"/dev/input"}},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{});

  delegate->remote()->MonitorTouchpad(std::move(observer));
  auto controller = std::make_unique<ProcessControl>(std::move(delegate));

  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&Executor::RunLongRunningDelegate,
                     weak_factory_.GetWeakPtr(), std::move(controller),
                     std::move(process_control_receiver)));
}

void Executor::RunAndWaitProcess(
    std::unique_ptr<brillo::Process> process,
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback,
    bool combine_stdout_and_stderr) {
  process->RedirectOutputToMemory(combine_stdout_and_stderr);
  process->Start();

  process_reaper_->WatchForChild(
      FROM_HERE, process->pid(),
      base::BindOnce(&Executor::OnRunAndWaitProcessFinished,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(process)));
}

void Executor::OnRunAndWaitProcessFinished(
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback,
    std::unique_ptr<brillo::Process> process,
    const siginfo_t& siginfo) {
  auto result = mojom::ExecutedProcessResult::New();

  result->return_code = siginfo.si_status;
  result->out = process->GetOutputString(STDOUT_FILENO);
  result->err = process->GetOutputString(STDERR_FILENO);

  process->Release();
  std::move(callback).Run(std::move(result));
}

void Executor::RunTrackedBinary(
    std::unique_ptr<brillo::Process> process,
    base::OnceCallback<void(ash::cros_healthd::mojom::ExecutedProcessResultPtr)>
        callback,
    const std::string& binary_path) {
  DCHECK(!tracked_processes_.count(binary_path));

  base::AutoLock auto_lock(lock_);

  process->RedirectOutputToMemory(false);
  process->Start();
  pid_t pid = process->pid();

  tracked_processes_[binary_path] = std::move(process);
  process_reaper_->WatchForChild(
      FROM_HERE, pid,
      base::BindOnce(&Executor::OnTrackedBinaryFinished,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     binary_path));
}

void Executor::OnTrackedBinaryFinished(
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback,
    const std::string& binary_path,
    const siginfo_t& siginfo) {
  auto result = mojom::ExecutedProcessResult::New();

  result->return_code = siginfo.si_status;
  result->out = tracked_processes_[binary_path]->GetOutputString(STDOUT_FILENO);
  result->err = tracked_processes_[binary_path]->GetOutputString(STDERR_FILENO);
  tracked_processes_[binary_path]->Release();
}

void Executor::RunLongRunningProcess(
    std::unique_ptr<brillo::Process> process,
    mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl> receiver,
    bool combine_stdout_and_stderr) {
  auto controller = std::make_unique<ProcessControl>(std::move(process));

  controller->RedirectOutputToMemory(combine_stdout_and_stderr);
  controller->StartAndWait(process_reaper_);
  process_control_set_.Add(std::move(controller), std::move(receiver));
}

void Executor::RunLongRunningDelegate(
    std::unique_ptr<ProcessControl> process_control,
    mojo::PendingReceiver<mojom::ProcessControl> receiver) {
  process_control->StartAndWait(process_reaper_);
  process_control_set_.Add(std::move(process_control), std::move(receiver));
}

}  // namespace diagnostics
