// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_mojo_service.h"

#include <inttypes.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
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

#include "diagnostics/cros_healthd/process/process_with_output.h"
#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "diagnostics/mojom/private/cros_healthd_executor.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd_executor::mojom;

// Amount of time we wait for a process to respond to SIGTERM before killing it.
constexpr base::TimeDelta kTerminationTimeout = base::TimeDelta::FromSeconds(2);

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
// wireless interface name start with "wl" and end it with a number. All
// characters are in lowercase.  Max length is 16 characters.
constexpr auto kWirelessInterfaceRegex = R"((wl[a-z][a-z0-9]{1,12}[0-9]))";

// SECCOMP policy for memtester, relative to kSandboxDirPath.
constexpr char kMemtesterSeccompPolicyPath[] = "memtester-seccomp.policy";
constexpr char kMemtesterBinary[] = "/usr/sbin/memtester";

// SECCOMP policy for modetest.
constexpr char kModetestSeccompPolicyPath[] = "modetest-seccomp.policy";
constexpr char kModetestBinary[] = "/usr/bin/modetest";

// Path to msr file. This file can be read by root only.
// Values of MSR registers IA32_TME_CAPABILITY (0x981) and IA32_TME_ACTIVATE_MSR
// (0x982) will be the same in all CPU cores. Therefore, we are only interested
// in reading the values in CPU0.
constexpr char kMsrPath[] = "/dev/cpu/0/msr";
// Fetch encryption data from following MSR registers IA32_TME_CAPABILITY
// (0x981) and IA32_TME_ACTIVATE_MSR (0x982) to report tme telemetry data.
constexpr std::array<uint32_t, 2> kMsrAccessAllowList{0x981, 0x982};

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
    mojo_ipc::ProcessResult mojo_result,
    base::OnceCallback<void(mojo_ipc::ProcessResultPtr)> callback) {
  std::move(callback).Run(mojo_result.Clone());
}

bool IsValidWirelessInterfaceName(const std::string& interface_name) {
  return (RE2::FullMatch(interface_name, kWirelessInterfaceRegex, nullptr));
}

bool IsMsrAccessAllowed(uint32_t msr) {
  for (auto it = kMsrAccessAllowList.begin(); it != kMsrAccessAllowList.end();
       it++) {
    if (*it == msr) {
      return true;
    }
  }
  return false;
}

}  // namespace

ExecutorMojoService::ExecutorMojoService(
    const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
    mojo::PendingReceiver<mojo_ipc::Executor> receiver)
    : mojo_task_runner_(mojo_task_runner),
      receiver_{this /* impl */, std::move(receiver)} {
  receiver_.set_disconnect_handler(
      base::BindOnce([]() { std::exit(EXIT_SUCCESS); }));
}

void ExecutorMojoService::GetFanSpeed(GetFanSpeedCallback callback) {
  mojo_ipc::ProcessResult result;

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
      &ExecutorMojoService::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, kEctoolUserAndGroup, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void ExecutorMojoService::GetInterfaces(GetInterfacesCallback callback) {
  mojo_ipc::ProcessResult result;

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
      &ExecutorMojoService::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, base::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void ExecutorMojoService::GetLink(const std::string& interface_name,
                                  GetLinkCallback callback) {
  mojo_ipc::ProcessResult result;
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
      &ExecutorMojoService::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, base::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void ExecutorMojoService::GetInfo(const std::string& interface_name,
                                  GetInfoCallback callback) {
  mojo_ipc::ProcessResult result;
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
      &ExecutorMojoService::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, base::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void ExecutorMojoService::GetScanDump(const std::string& interface_name,
                                      GetScanDumpCallback callback) {
  mojo_ipc::ProcessResult result;
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
      &ExecutorMojoService::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, base::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void ExecutorMojoService::RunMemtester(RunMemtesterCallback callback) {
  mojo_ipc::ProcessResult result;

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
      base::BindOnce(&ExecutorMojoService::RunTrackedBinary,
                     weak_factory_.GetWeakPtr(), kSeccompPolicyPath,
                     sandboxing_args, base::nullopt,
                     base::FilePath(kMemtesterBinary), memtester_args,
                     std::move(result), std::move(callback)));
}

void ExecutorMojoService::KillMemtester() {
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

void ExecutorMojoService::GetProcessIOContents(
    const uint32_t pid, GetProcessIOContentsCallback callback) {
  std::string result;

  ReadAndTrimString(base::FilePath("/proc/")
                        .Append(base::StringPrintf("%" PRId32, pid))
                        .AppendASCII("io"),
                    &result);

  std::move(callback).Run(result);
}

void ExecutorMojoService::RunModetest(mojo_ipc::ModetestOptionEnum option,
                                      RunModetestCallback callback) {
  mojo_ipc::ProcessResult result;
  std::vector<std::string> binary_args;

  switch (option) {
    case mojo_ipc::ModetestOptionEnum::kListConnector:
      binary_args.push_back("-c");
      break;
    default:
      result.return_code = EXIT_FAILURE;
      result.err = "Unsupported option";
      std::move(callback).Run(result.Clone());
      return;
  }

  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(kModetestSeccompPolicyPath);

  // Minijail setup for modetest.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-G");

  base::FilePath binary_path = base::FilePath(kModetestBinary);

  base::OnceClosure closure = base::BindOnce(
      &ExecutorMojoService::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, base::nullopt, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void ExecutorMojoService::ReadMsr(const uint32_t msr_reg,
                                  ReadMsrCallback callback) {
  mojo_ipc::ProcessResult status;
  uint64_t val = 0;
  if (!IsMsrAccessAllowed(msr_reg)) {
    status.return_code = EXIT_FAILURE;
    status.err = "MSR access not allowed";
    std::move(callback).Run(status.Clone(), val);
    return;
  }
  base::File msr_fd(base::FilePath(kMsrPath),
                    base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!msr_fd.IsValid()) {
    status.return_code = EXIT_FAILURE;
    status.err = "Could not open " + std::string(kMsrPath);
    std::move(callback).Run(status.Clone(), val);
    return;
  }
  char msr_buf[sizeof(uint64_t)];
  // Read MSR register.
  if (sizeof(msr_buf) != msr_fd.Read(msr_reg, msr_buf, sizeof(msr_buf))) {
    status.return_code = EXIT_FAILURE;
    status.err = "Could not read MSR register from " + std::string(kMsrPath);
    std::move(callback).Run(status.Clone(), val);
    return;
  }
  val = *reinterpret_cast<uint64_t*>(&msr_buf[0]);
  status.return_code = EXIT_SUCCESS;
  std::move(callback).Run(status.Clone(), val);
}

void ExecutorMojoService::GetUEFISecureBootContent(
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

void ExecutorMojoService::RunUntrackedBinary(
    const base::FilePath& seccomp_policy_path,
    const std::vector<std::string>& sandboxing_args,
    const base::Optional<std::string>& user,
    const base::FilePath& binary_path,
    const std::vector<std::string>& binary_args,
    mojo_ipc::ProcessResult result,
    base::OnceCallback<void(mojo_ipc::ProcessResultPtr)> callback) {
  auto process = std::make_unique<ProcessWithOutput>();
  result.return_code =
      RunBinaryInternal(seccomp_policy_path, sandboxing_args, user, binary_path,
                        binary_args, &result, process.get());
  mojo_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&RunMojoProcessResultCallback,
                                std::move(result), std::move(callback)));
}

void ExecutorMojoService::RunTrackedBinary(
    const base::FilePath& seccomp_policy_path,
    const std::vector<std::string>& sandboxing_args,
    const base::Optional<std::string>& user,
    const base::FilePath& binary_path,
    const std::vector<std::string>& binary_args,
    mojo_ipc::ProcessResult result,
    base::OnceCallback<void(mojo_ipc::ProcessResultPtr)> callback) {
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

int ExecutorMojoService::RunBinaryInternal(
    const base::FilePath& seccomp_policy_path,
    const std::vector<std::string>& sandboxing_args,
    const base::Optional<std::string>& user,
    const base::FilePath& binary_path,
    const std::vector<std::string>& binary_args,
    mojo_ipc::ProcessResult* result,
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
