// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor.h"

#include <inttypes.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/executor/utils/delegate_process.h"
#include "diagnostics/cros_healthd/process/process_with_output.h"
#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Amount of time we wait for a process to respond to SIGTERM before killing it.
constexpr base::TimeDelta kTerminationTimeout = base::Seconds(2);

// Null capability for delegate process.
constexpr uint64_t kNullCapability = 0;

// All SECCOMP policies should live in this directory.
constexpr char kSandboxDirPath[] = "/usr/share/policy/";

// SECCOMP policy for fingerprint related routines.
constexpr char kFingerprintSeccompPolicyPath[] = "fingerprint-seccomp.policy";
constexpr char kFingerprintUserAndGroup[] = "healthd_fp";

// SECCOMP policy for ectool pwmgetfanrpm:
constexpr char kFanSpeedSeccompPolicyPath[] =
    "ectool_pwmgetfanrpm-seccomp.policy";
constexpr char kEctoolUserAndGroup[] = "healthd_ec";
constexpr char kEctoolBinary[] = "/usr/sbin/ectool";
// The ectool command used to collect fan speed in RPM.
constexpr char kGetFanRpmCommand[] = "pwmgetfanrpm";

// SECCOMP policy for ectool motionsense lid_angle:
constexpr char kLidAngleSeccompPolicyPath[] =
    "ectool_motionsense_lid_angle-seccomp.policy";
// The ectool commands used to collect lid angle.
constexpr char kMotionSenseCommand[] = "motionsense";
constexpr char kLidAngleCommand[] = "lid_angle";

// The iw command used to collect different wireless data.
constexpr char kIwSeccompPolicyPath[] = "iw-seccomp.policy";
// constexpr char kIwUserAndGroup[] = "healthd_iw";
constexpr char kIwBinary[] = "/usr/sbin/iw";
constexpr char kIwInterfaceCommand[] = "dev";
constexpr char kIwInfoCommand[] = "info";
constexpr char kIwLinkCommand[] = "link";
constexpr std::array<const char*, 2> kIwScanDumpCommand{"scan", "dump"};
// wireless interface name start with "wl" or "ml" and end it with a number. All
// characters are in lowercase.  Max length is 16 characters.
constexpr auto kWirelessInterfaceRegex = R"(([wm]l[a-z][a-z0-9]{1,12}[0-9]))";

// SECCOMP policy for memtester, relative to kSandboxDirPath.
constexpr char kMemtesterSeccompPolicyPath[] = "memtester-seccomp.policy";
constexpr char kMemtesterBinary[] = "/usr/sbin/memtester";

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

// All Mojo callbacks need to be ran by the Mojo task runner, so this provides a
// convenient wrapper that can be bound and ran by that specific task runner.
void RunMojoProcessResultCallback(
    mojom::ExecutedProcessResult mojo_result,
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback) {
  std::move(callback).Run(mojo_result.Clone());
}

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

void GetFingerprintFrameCallback(
    base::OnceCallback<void(mojom::FingerprintFrameResultPtr,
                            const std::optional<std::string>&)> callback,
    std::unique_ptr<DelegateProcess> delegate,
    mojom::FingerprintFrameResultPtr result,
    const std::optional<std::string>& err) {
  delegate.reset();
  std::move(callback).Run(std::move(result), err);
}

void GetFingerprintFrameTask(
    mojom::FingerprintCaptureType type,
    base::OnceCallback<void(mojom::FingerprintFrameResultPtr,
                            const std::optional<std::string>&)> callback) {
  auto delegate = std::make_unique<DelegateProcess>(
      kFingerprintSeccompPolicyPath, kFingerprintUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{base::FilePath{fingerprint::kCrosFpPath}});

  auto cb = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::FingerprintFrameResult::New(),
      kFailToLaunchDelegate);
  delegate->remote()->GetFingerprintFrame(
      type, base::BindOnce(&GetFingerprintFrameCallback, std::move(cb),
                           std::move(delegate)));
}

void GetFingerprintInfoCallback(
    base::OnceCallback<void(mojom::FingerprintInfoResultPtr,
                            const std::optional<std::string>&)> callback,
    std::unique_ptr<DelegateProcess> delegate,
    mojom::FingerprintInfoResultPtr result,
    const std::optional<std::string>& err) {
  delegate.reset();
  std::move(callback).Run(std::move(result), err);
}

void GetFingerprintInfoTask(
    base::OnceCallback<void(mojom::FingerprintInfoResultPtr,
                            const std::optional<std::string>&)> callback) {
  auto delegate = std::make_unique<DelegateProcess>(
      kFingerprintSeccompPolicyPath, kFingerprintUserAndGroup, kNullCapability,
      /*readonly_mount_points=*/std::vector<base::FilePath>{},
      /*writable_mount_points=*/
      std::vector<base::FilePath>{base::FilePath{fingerprint::kCrosFpPath}});

  auto cb = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::FingerprintInfoResult::New(),
      kFailToLaunchDelegate);
  delegate->remote()->GetFingerprintInfo(base::BindOnce(
      &GetFingerprintInfoCallback, std::move(cb), std::move(delegate)));
}

}  // namespace

// Exported for testing.
bool IsValidWirelessInterfaceName(const std::string& interface_name) {
  return (RE2::FullMatch(interface_name, kWirelessInterfaceRegex, nullptr));
}

Executor::Executor(
    const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
    mojo::PendingReceiver<mojom::Executor> receiver,
    base::OnceClosure on_disconnect)
    : mojo_task_runner_(mojo_task_runner),
      receiver_{this /* impl */, std::move(receiver)} {
  receiver_.set_disconnect_handler(std::move(on_disconnect));
}

void Executor::GetFanSpeed(GetFanSpeedCallback callback) {
  mojom::ExecutedProcessResult result;

  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(kFanSpeedSeccompPolicyPath);

  // Minijail setup for ectool.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-G");
  sandboxing_args.push_back("-c");
  sandboxing_args.push_back("cap_sys_rawio=e");
  sandboxing_args.push_back("-b");
  sandboxing_args.push_back("/dev/cros_ec");

  std::vector<std::string> binary_args = {kGetFanRpmCommand};
  base::FilePath binary_path = base::FilePath(kEctoolBinary);

  base::OnceClosure closure = base::BindOnce(
      &Executor::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, kEctoolUserAndGroup, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void Executor::GetInterfaces(GetInterfacesCallback callback) {
  mojom::ExecutedProcessResult result;

  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(kIwSeccompPolicyPath);

  // Minijail setup for iw.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-G");
  sandboxing_args.push_back("-b");
  sandboxing_args.push_back("/usr/sbin/iw");

  std::vector<std::string> binary_args = {kIwInterfaceCommand};
  base::FilePath binary_path = base::FilePath(kIwBinary);

  // Since no user:group is specified, this will run with the default
  // cros_healthd:cros_healthd user and group.
  base::OnceClosure closure = base::BindOnce(
      &Executor::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, std::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void Executor::GetLink(const std::string& interface_name,
                       GetLinkCallback callback) {
  mojom::ExecutedProcessResult result;
  // Sanitize against interface_name.
  if (!IsValidWirelessInterfaceName(interface_name)) {
    result.err = "Illegal interface name: " + interface_name;
    result.return_code = EXIT_FAILURE;
    std::move(callback).Run(result.Clone());
    return;
  }

  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(kIwSeccompPolicyPath);

  // Minijail setup for iw.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-G");
  sandboxing_args.push_back("-b");
  sandboxing_args.push_back("/usr/sbin/iw");

  std::vector<std::string> binary_args;
  binary_args.push_back(interface_name);
  binary_args.push_back(kIwLinkCommand);

  base::FilePath binary_path = base::FilePath(kIwBinary);

  // Since no user:group is specified, this will run with the default
  // cros_healthd:cros_healthd user and group.
  base::OnceClosure closure = base::BindOnce(
      &Executor::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, std::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void Executor::GetInfo(const std::string& interface_name,
                       GetInfoCallback callback) {
  mojom::ExecutedProcessResult result;
  // Sanitize against interface_name.
  if (!IsValidWirelessInterfaceName(interface_name)) {
    result.err = "Illegal interface name: " + interface_name;
    result.return_code = EXIT_FAILURE;
    std::move(callback).Run(result.Clone());
    return;
  }

  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(kIwSeccompPolicyPath);

  // Minijail setup for iw.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-G");
  sandboxing_args.push_back("-b");
  sandboxing_args.push_back("/usr/sbin/iw");

  std::vector<std::string> binary_args;
  binary_args.push_back(interface_name);
  binary_args.push_back(kIwInfoCommand);

  base::FilePath binary_path = base::FilePath(kIwBinary);

  // Since no user:group is specified, this will run with the default
  // cros_healthd:cros_healthd user and group.
  base::OnceClosure closure = base::BindOnce(
      &Executor::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, std::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void Executor::GetScanDump(const std::string& interface_name,
                           GetScanDumpCallback callback) {
  mojom::ExecutedProcessResult result;
  // Sanitize against interface_name.
  if (!IsValidWirelessInterfaceName(interface_name)) {
    result.err = "Illegal interface name: " + interface_name;
    result.return_code = EXIT_FAILURE;
    std::move(callback).Run(result.Clone());
    return;
  }

  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(kIwSeccompPolicyPath);

  // Minijail setup for iw.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-G");
  sandboxing_args.push_back("-b");
  sandboxing_args.push_back("/usr/sbin/iw");

  std::vector<std::string> binary_args;
  binary_args.push_back(interface_name);
  binary_args.insert(binary_args.end(), kIwScanDumpCommand.begin(),
                     kIwScanDumpCommand.end());

  base::FilePath binary_path = base::FilePath(kIwBinary);

  // Since no user:group is specified, this will run with the default
  // cros_healthd:cros_healthd user and group.
  base::OnceClosure closure = base::BindOnce(
      &Executor::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, std::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void Executor::RunMemtester(uint32_t test_mem_kib,
                            RunMemtesterCallback callback) {
  mojom::ExecutedProcessResult result;

  // TODO(b/193211343): Design a mechanism for multiple resource intensive task.
  // Only allow one instance of memtester at a time. This is reasonable, because
  // memtester mlocks almost the entirety of the device's memory, and a second
  // memtester process wouldn't have any memory to test.
  auto itr = processes_.find(kMemtesterBinary);
  if (itr != processes_.end()) {
    result.return_code = MemtesterErrorCodes::kAllocatingLockingInvokingError;
    result.err = kMemoryRoutineMemtesterAlreadyRunningMessage;
    std::move(callback).Run(result.Clone());
    return;
  }

  // Minijail setup for memtester.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-c");
  sandboxing_args.push_back("cap_ipc_lock=e");

  // Additional args for memtester.
  std::vector<std::string> memtester_args;
  // Run with all free memory, except that which we left to the operating system
  // above.
  memtester_args.push_back(base::StringPrintf("%uK", test_mem_kib));
  // Run for one loop.
  memtester_args.push_back("1");

  const auto kSeccompPolicyPath =
      base::FilePath(kSandboxDirPath).Append(kMemtesterSeccompPolicyPath);

  // Since no user:group is specified, this will run with the default
  // cros_healthd:cros_healthd user and group.
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&Executor::RunTrackedBinary, weak_factory_.GetWeakPtr(),
                     kSeccompPolicyPath, sandboxing_args, std::nullopt,
                     base::FilePath(kMemtesterBinary), memtester_args,
                     std::move(result), std::move(callback)));
}

void Executor::KillMemtester() {
  base::AutoLock auto_lock(lock_);
  auto itr = processes_.find(kMemtesterBinary);
  if (itr == processes_.end())
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
                                .Append(std::to_string(cpu_index))
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
  mojom::ExecutedProcessResult result;

  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(kLidAngleSeccompPolicyPath);

  // Minijail setup for ectool.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-G");
  sandboxing_args.push_back("-b");
  sandboxing_args.push_back("/dev/cros_ec");

  std::vector<std::string> binary_args = {kMotionSenseCommand,
                                          kLidAngleCommand};
  base::FilePath binary_path = base::FilePath(kEctoolBinary);

  base::OnceClosure closure = base::BindOnce(
      &Executor::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, kEctoolUserAndGroup, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void Executor::GetFingerprintFrame(mojom::FingerprintCaptureType type,
                                   GetFingerprintFrameCallback callback) {
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&GetFingerprintFrameTask, type, std::move(callback)));
}

void Executor::GetFingerprintInfo(GetFingerprintInfoCallback callback) {
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&GetFingerprintInfoTask, std::move(callback)));
}

void Executor::RunUntrackedBinary(
    const base::FilePath& seccomp_policy_path,
    const std::vector<std::string>& sandboxing_args,
    const std::optional<std::string>& user,
    const base::FilePath& binary_path,
    const std::vector<std::string>& binary_args,
    mojom::ExecutedProcessResult result,
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback) {
  auto process = std::make_unique<ProcessWithOutput>();
  result.return_code =
      RunBinaryInternal(seccomp_policy_path, sandboxing_args, user, binary_path,
                        binary_args, &result, process.get());
  mojo_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&RunMojoProcessResultCallback,
                                std::move(result), std::move(callback)));
}

void Executor::RunTrackedBinary(
    const base::FilePath& seccomp_policy_path,
    const std::vector<std::string>& sandboxing_args,
    const std::optional<std::string>& user,
    const base::FilePath& binary_path,
    const std::vector<std::string>& binary_args,
    mojom::ExecutedProcessResult result,
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback) {
  std::string binary_path_str = binary_path.value();
  DCHECK(!processes_.count(binary_path_str));

  {
    auto process = std::make_unique<ProcessWithOutput>();
    base::AutoLock auto_lock(lock_);
    processes_[binary_path_str] = std::move(process);
  }

  result.return_code = RunBinaryInternal(
      seccomp_policy_path, sandboxing_args, user, binary_path, binary_args,
      &result, processes_[binary_path_str].get());

  // Get rid of the process.
  base::AutoLock auto_lock(lock_);
  auto itr = processes_.find(binary_path_str);
  DCHECK(itr != processes_.end());
  processes_.erase(itr);
  mojo_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&RunMojoProcessResultCallback,
                                std::move(result), std::move(callback)));
}

int Executor::RunBinaryInternal(const base::FilePath& seccomp_policy_path,
                                const std::vector<std::string>& sandboxing_args,
                                const std::optional<std::string>& user,
                                const base::FilePath& binary_path,
                                const std::vector<std::string>& binary_args,
                                mojom::ExecutedProcessResult* result,
                                ProcessWithOutput* process) {
  DCHECK(result);
  DCHECK(process);

  if (!base::PathExists(seccomp_policy_path)) {
    result->err = "Sandbox info is missing for this architecture.";
    return EXIT_FAILURE;
  }

  // Sandboxing setup for the process.
  if (user.has_value())
    process->SandboxAs(user.value(), user.value());
  process->SetSeccompFilterPolicyFile(seccomp_policy_path.MaybeAsASCII());
  process->set_separate_stderr(true);
  if (!process->Init(sandboxing_args)) {
    result->err = "Process initialization failure.";
    return EXIT_FAILURE;
  }

  process->AddArg(binary_path.MaybeAsASCII());
  for (const auto& arg : binary_args)
    process->AddArg(arg);
  int exit_code = process->Run();
  if (exit_code != EXIT_SUCCESS) {
    process->GetError(&result->err);
    return exit_code;
  }

  if (!process->GetOutput(&result->out)) {
    result->err = "Failed to get output from process.";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

}  // namespace diagnostics
