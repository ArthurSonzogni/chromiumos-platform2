// Copyright 2020 The Chromium OS Authors. All rights reserved.
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
#include <base/system/sys_info.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/process/process_with_output.h"
#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

// Amount of time we wait for a process to respond to SIGTERM before killing it.
constexpr base::TimeDelta kTerminationTimeout = base::Seconds(2);

// All SECCOMP policies should live in this directory.
constexpr char kSandboxDirPath[] = "/usr/share/policy/";
// SECCOMP policy for ectool pwmgetfanrpm:
constexpr char kFanSpeedSeccompPolicyPath[] =
    "ectool_pwmgetfanrpm-seccomp.policy";
constexpr char kEctoolUserAndGroup[] = "healthd_ec";
constexpr char kEctoolBinary[] = "/usr/sbin/ectool";
// The ectool command used to collect fan speed in RPM.
constexpr char kGetFanRpmCommand[] = "pwmgetfanrpm";

// The iw command used to collect diffrent wireless data.
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
    "/sys/firmware/efi/vars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c/"
    "data";

// All Mojo callbacks need to be ran by the Mojo task runner, so this provides a
// convenient wrapper that can be bound and ran by that specific task runner.
void RunMojoProcessResultCallback(
    mojom::ExecutedProcessResult mojo_result,
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback) {
  std::move(callback).Run(mojo_result.Clone());
}

}  // namespace

// Exported for testing.
bool IsValidWirelessInterfaceName(const std::string& interface_name) {
  return (RE2::FullMatch(interface_name, kWirelessInterfaceRegex, nullptr));
}

Executor::Executor(
    const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
    mojo::PendingReceiver<mojom::Executor> receiver)
    : mojo_task_runner_(mojo_task_runner),
      receiver_{this /* impl */, std::move(receiver)} {
  receiver_.set_disconnect_handler(
      base::BindOnce([]() { std::exit(EXIT_SUCCESS); }));
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

void Executor::RunMemtester(RunMemtesterCallback callback) {
  mojom::ExecutedProcessResult result;

  // TODO(b/193211343): Design a mechanism for multiple resource intensive task.
  // Only allow one instance of memtester at a time. This is reasonable, because
  // memtester mlocks almost the entirety of the device's memory, and a second
  // memtester process wouldn't have any memory to test.
  auto itr = processes_.find(kMemtesterBinary);
  if (itr != processes_.end()) {
    result.return_code = MemtesterErrorCodes::kAllocatingLockingInvokingError;
    result.err = "Memtester process already running.";
    std::move(callback).Run(result.Clone());
    return;
  }

  // Get AvailablePhysicalMemory in MiB.
  int64_t available_mem = base::SysInfo::AmountOfAvailablePhysicalMemory();
  available_mem /= (1024 * 1024);

  available_mem -= kMemoryRoutineReservedSizeMiB;
  if (available_mem <= 0) {
    result.err = "Not enough available memory to run memtester.";
    result.return_code = MemtesterErrorCodes::kAllocatingLockingInvokingError;
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
  memtester_args.push_back(base::StringPrintf("%" PRId64, available_mem));
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

void Executor::GetProcessIOContents(const uint32_t pid,
                                    GetProcessIOContentsCallback callback) {
  std::string result;

  ReadAndTrimString(base::FilePath("/proc/")
                        .Append(base::StringPrintf("%" PRId32, pid))
                        .AppendASCII("io"),
                    &result);

  std::move(callback).Run(result);
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
  std::string content;

  base::FilePath f = base::FilePath(kUEFISecureBootVarPath);
  if (!base::ReadFileToString(f, &content)) {
    LOG(ERROR) << "Failed to read file: " << f.value();
    std::move(callback).Run("");
    return;
  }

  std::move(callback).Run(content);
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
