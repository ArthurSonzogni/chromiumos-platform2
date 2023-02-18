// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/sandboxed_process.h"

#include <inttypes.h>
#include <signal.h>
#include <sys/wait.h>

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "diagnostics/base/file_utils.h"

namespace diagnostics {

namespace {

uint32_t FetchJailedProcessPid(uint32_t parent_pid) {
  base::FilePath pid_file = base::FilePath("/proc")
                                .Append(base::NumberToString(parent_pid))
                                .Append("task")
                                .Append(base::NumberToString(parent_pid))
                                .Append("children");

  std::string pid_str;
  if (!ReadAndTrimString(pid_file, &pid_str)) {
    return 0;
  }

  // We assume that the minijail only has one child process.
  uint32_t pid;
  if (!base::StringToUint(pid_str, &pid)) {
    return 0;
  }

  return pid;
}

}  // namespace

SandboxedProcess::SandboxedProcess() = default;

SandboxedProcess::~SandboxedProcess() {
  const uint8_t timeout = 3;
  // Send SIGTERM first to prevent minijail show the warning message of SIGKILL.
  if (KillJailedProcess(SIGTERM, timeout)) {
    return;
  }
  KillJailedProcess(SIGKILL, timeout);
}

SandboxedProcess::SandboxedProcess(
    const std::vector<std::string>& command,
    const std::string& seccomp_filename,
    const std::string& user,
    uint64_t capabilities_mask,
    const std::vector<base::FilePath>& readonly_mount_points,
    const std::vector<base::FilePath>& writable_mount_points,
    uint32_t sandbox_option)
    : command_(command), readonly_mount_points_(readonly_mount_points) {
  auto seccomp_file =
      base::FilePath(kSeccompPolicyDirectory).Append(seccomp_filename);
  sandbox_arguments_ = {
      // Enter a new VFS mount namespace.
      "-v",
      // Remount /proc readonly.
      "-r",
      // Run inside a new IPC namespace.
      "-l",
      // Create a new UTS/hostname namespace.
      "--uts",
      // Set user.
      "-u",
      user,
      // Set group. The group is assume to be the same as user.
      "-g",
      user,
      // Inherit all the supplementary groups of the user specified with -u.
      "-G",
      // Restrict capabilities.
      "-c",
      base::StringPrintf("0x%" PRIx64, capabilities_mask),
      // Set seccomp policy file.
      "-S",
      seccomp_file.value(),
      // Set the process’s no_new_privs bit.
      "-n",
  };

  if ((sandbox_option & NO_ENTER_NETWORK_NAMESPACE) == 0) {
    // Enter a new network namespace.
    sandbox_arguments_.push_back("-e");
  }
  for (const base::FilePath& f : writable_mount_points) {
    sandbox_arguments_.push_back("-b");
    sandbox_arguments_.push_back(f.value() + "," + f.value() + ",1");
  }
}

SandboxedProcess::SandboxedProcess(
    const std::vector<std::string>& command,
    const std::string& seccomp_filename,
    const std::vector<base::FilePath>& readonly_mount_points)
    : SandboxedProcess(command,
                       seccomp_filename,
                       kCrosHealthdSandboxUser,
                       /*capabilities_mask=*/0x0,
                       readonly_mount_points,
                       /*writable_mount_points=*/{}) {}

void SandboxedProcess::AddArg(const std::string& arg) {
  command_.push_back(arg);
}

bool SandboxedProcess::Start() {
  PrepareSandboxArguments();

  BrilloProcessAddArg(kMinijailBinary);
  for (const std::string& arg : sandbox_arguments_) {
    BrilloProcessAddArg(arg);
  }
  BrilloProcessAddArg("--");
  for (const std::string& arg : command_) {
    BrilloProcessAddArg(arg);
  }
  return BrilloProcessStart();
}

bool SandboxedProcess::KillJailedProcess(int signal, uint8_t timeout) {
  if (pid() == 0) {
    // Passing pid == 0 to kill is committing suicide.  Check specifically.
    LOG(ERROR) << "Process not running";
    return false;
  }

  uint32_t jailed_process_pid = 0;
  base::TimeTicks start_signal = base::TimeTicks::Now();
  do {
    if (jailed_process_pid == 0) {
      jailed_process_pid = FetchJailedProcessPid(pid());
      if (jailed_process_pid != 0 && kill(jailed_process_pid, signal) < 0) {
        PLOG(ERROR) << "Unable to send signal to " << jailed_process_pid;
        return false;
      }
    }

    int status = 0;
    pid_t w = waitpid(pid(), &status, WNOHANG);
    if (w < 0) {
      if (errno == ECHILD) {
        UpdatePid(0);
        return true;
      }
      PLOG(ERROR) << "Waitpid returned " << w;
      return false;
    }

    // In normal case, the first |w| we receive is the pid of jailed process. We
    // still need to wait until the minijail process is terminated. Once it's
    // done, we update the pid to 0 so the brillo::Process won't kill the
    // process again.
    if (w == pid()) {
      UpdatePid(0);
      return true;
    }
    usleep(100);
  } while ((base::TimeTicks::Now() - start_signal).InSecondsF() <= timeout);

  return false;
}

// Prepares some arguments which need to be handled before use.
void SandboxedProcess::PrepareSandboxArguments() {
  for (const base::FilePath& f : readonly_mount_points_) {
    if (!IsPathExists(f)) {
      DLOG(INFO) << "Try to mount a file which doesn't exist: " << f;
      continue;
    }
    sandbox_arguments_.push_back("-b");
    sandbox_arguments_.push_back(f.value());
  }
}

void SandboxedProcess::BrilloProcessAddArg(const std::string& arg) {
  brillo::ProcessImpl::AddArg(arg);
}

bool SandboxedProcess::BrilloProcessStart() {
  return brillo::ProcessImpl::Start();
}

// Checks if a file exist. For mocking.
bool SandboxedProcess::IsPathExists(const base::FilePath& path) const {
  return base::PathExists(path);
}

}  // namespace diagnostics
