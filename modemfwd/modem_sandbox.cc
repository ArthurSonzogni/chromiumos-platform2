// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_sandbox.h"

#include <string>
#include <vector>

#include <linux/securebits.h>
#include <signal.h>
#include <stdio.h>

#include <base/no_destructor.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/platform_file.h>
#include <base/logging.h>
#include <base/process/process.h>
#include <base/time/time.h>
#include <libminijail.h>
#include <scoped_minijail.h>

#include "modemfwd/logging.h"

namespace modemfwd {

namespace {

int GetLoggingFd() {
  static base::NoDestructor<base::File> s_minijail_log_file([] {
    base::FilePath log_dir(kModemfwdLogDirectory);
    base::FilePath previous_path = log_dir.Append("minijail.previous");
    base::FilePath current_path = log_dir.Append("minijail.current");
    if (base::PathExists(current_path) &&
        !base::Move(current_path, previous_path)) {
      LOG(WARNING) << "Failed to rotate minijail log, logs from the previous"
                      "boot will be lost";
    }
    return base::File(current_path,
                      base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  }());
  return s_minijail_log_file->GetPlatformFile();
}

int SetupLogging(void* payload) {
  base::Time::Exploded now;
  base::Time::Now().UTCExplode(&now);

  intptr_t fd = reinterpret_cast<intptr_t>(payload);
  dprintf(fd,
          "%4d-%02d-%02dT%02d:%02d:%02d.%06dZ starting minijailed process %d\n",
          now.year, now.month, now.day_of_month, now.hour, now.minute,
          now.second, now.millisecond, getpid());
  // LOG_INFO = 6. We can't include syslog.h here because it causes conflicts
  // with base/logging.h.
  minijail_log_to_fd(fd, 6);
  return 0;
}

}  // namespace

// For security reasons, we want to apply security restrictions to utilities:
// 1. We want to provide net admin capabilities only when necessary.
// 2. We want to apply utility-specific seccomp filter.
ScopedMinijail ConfigureSandbox(const base::FilePath& seccomp_file_path,
                                bool should_remove_capabilities) {
  ScopedMinijail j(minijail_new());

  // Ensure no capability escalation occurs in the jail.
  minijail_no_new_privs(j.get());

  // Avoid setting securebits as we are running inside a minijail already.
  // See b/112030238 for justification.
  minijail_skip_setting_securebits(j.get(), SECURE_ALL_BITS | SECURE_ALL_LOCKS);

  // Remove all capabilities if the process doesn't require cap_net_admin by
  // setting sandboxed capabilities to 0. Only the FM350 helper requires
  // cap_net_admin.
  if (should_remove_capabilities)
    minijail_use_caps(j.get(), 0);

  // Apply seccomp filter, if it exists.
  if (base::PathExists(seccomp_file_path)) {
    minijail_use_seccomp_filter(j.get());
    minijail_parse_seccomp_filters(j.get(), seccomp_file_path.value().c_str());
  } else {
    LOG(WARNING) << "Minijail configured without seccomp filter";
  }

  int logging_fd = GetLoggingFd();
  minijail_preserve_fd(j.get(), logging_fd, logging_fd);
  minijail_add_hook(j.get(), &SetupLogging,
                    reinterpret_cast<void*>(static_cast<intptr_t>(logging_fd)),
                    MINIJAIL_HOOK_EVENT_PRE_DROP_CAPS);

  return j;
}

int RunProcessInSandboxWithTimeout(
    const std::vector<std::string>& formatted_args,
    const base::FilePath& seccomp_file_path,
    bool should_remove_capabilities,
    int* child_stdout,
    int* child_stderr,
    base::TimeDelta timeout) {
  pid_t pid = -1;
  std::vector<char*> args;

  for (const std::string& argument : formatted_args)
    args.push_back(const_cast<char*>(argument.c_str()));

  args.push_back(nullptr);

  // Create sandbox and run process.
  ScopedMinijail j =
      ConfigureSandbox(seccomp_file_path, should_remove_capabilities);
  int ret = minijail_run_pid_pipes_no_preload(
      j.get(), args[0], args.data(), &pid, nullptr, child_stdout, child_stderr);

  if (ret != 0) {
    LOG(ERROR) << "Failed to run minijail: " << strerror(-ret);
    return ret;
  }

  // If the timeout provided is zero, we block until the command is finished
  // and return its exit code.
  if (timeout.is_zero())
    return minijail_wait(j.get());

  auto process = base::Process::Open(pid);
  int exit_code = -1;

  // Allow process to complete normally
  if (process.WaitForExitWithTimeout(timeout, &exit_code))
    return exit_code;

  LOG(ERROR) << "Child process timed out";

  // Try to terminate it gracefully
  kill(pid, SIGTERM);
  if (process.WaitForExitWithTimeout(timeout, nullptr))
    return -MINIJAIL_ERR_SIG_BASE;

  // Kill it
  kill(pid, SIGKILL);
  process.WaitForExitWithTimeout(timeout, nullptr);
  return -MINIJAIL_ERR_SIG_BASE;
}

int RunProcessInSandbox(const std::vector<std::string>& formatted_args,
                        const base::FilePath& seccomp_file_path,
                        bool should_remove_capabilities,
                        int* child_stdout,
                        int* child_stderr) {
  return RunProcessInSandboxWithTimeout(
      formatted_args, seccomp_file_path, should_remove_capabilities,
      child_stdout, child_stderr, base::TimeDelta() /* timeout */);
}

}  // namespace modemfwd
