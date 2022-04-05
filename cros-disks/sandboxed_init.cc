// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/sandboxed_init.h"

#include <utility>

#include <poll.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/libminijail.h>

namespace cros_disks {
namespace {

// Signal handler that forwards the received signal to all processes.
void SigTerm(int sig) {
  RAW_CHECK(sig == SIGTERM);
  RAW_LOG(INFO, "The 'init' process received SIGTERM");
  if (kill(-1, SIGTERM) < 0) {
    const int err = errno;
    RAW_LOG(ERROR, "Cannot broadcast SIGTERM");
    _exit(err + 64);
  }
}

}  // namespace

SubprocessPipe::SubprocessPipe(const Direction direction) {
  int fds[2];
  PCHECK(pipe(fds) >= 0);
  child_fd.reset(fds[1 - direction]);
  parent_fd.reset(fds[direction]);
  PCHECK(base::SetCloseOnExec(parent_fd.get()));
}

base::ScopedFD SubprocessPipe::Open(const Direction direction,
                                    base::ScopedFD* const parent_fd) {
  DCHECK(parent_fd);

  SubprocessPipe p(direction);
  *parent_fd = std::move(p.parent_fd);
  return std::move(p.child_fd);
}

SandboxedInit::SandboxedInit(base::ScopedFD in_fd,
                             base::ScopedFD out_fd,
                             base::ScopedFD err_fd,
                             base::ScopedFD ctrl_fd)
    : in_fd_(std::move(in_fd)),
      out_fd_(std::move(out_fd)),
      err_fd_(std::move(err_fd)),
      ctrl_fd_(std::move(ctrl_fd)) {}

SandboxedInit::~SandboxedInit() = default;

[[noreturn]] void SandboxedInit::RunInsideSandboxNoReturn(Launcher launcher) {
  // To run our custom init that handles daemonized processes inside the
  // sandbox we have to set up fork/exec ourselves. We do error-handling
  // the "minijail-style" - abort if something not right.

  // This performs as the init process in the jail PID namespace (PID 1).
  // Redirect in/out so logging can communicate assertions and children
  // to inherit right FDs.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  if (dup2(in_fd_.get(), STDIN_FILENO) < 0)
    PLOG(FATAL) << "Cannot dup2 stdin";

  if (dup2(out_fd_.get(), STDOUT_FILENO) < 0)
    PLOG(FATAL) << "Cannot dup2 stdout";

  if (dup2(err_fd_.get(), STDERR_FILENO) < 0)
    PLOG(FATAL) << "Cannot dup2 stderr";

  // Set an identifiable process name.
  if (prctl(PR_SET_NAME, "cros-disks-INIT") < 0)
    PLOG(WARNING) << "Cannot set init's process name";

  // Close unused file descriptors.
  in_fd_.reset();
  out_fd_.reset();
  err_fd_.reset();

  // Setup the SIGTERM signal handler.
  if (signal(SIGTERM, SigTerm) == SIG_ERR)
    PLOG(FATAL) << "Cannot install SIGTERM signal handler";

  // PID of the launcher process inside the jail PID namespace (e.g. PID 2).
  pid_t launcher_pid = StartLauncher(std::move(launcher));
  CHECK_LT(0, launcher_pid);

  _exit(RunInitLoop(launcher_pid, std::move(ctrl_fd_)));
  NOTREACHED();
}

int SandboxedInit::RunInitLoop(pid_t launcher_pid, base::ScopedFD ctrl_fd) {
  // Set up the SIGPIPE signal handler. Since we write to the control pipe, we
  // don't want this 'init' process to be killed by a SIGPIPE.
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    PLOG(FATAL) << "Cannot install SIGPIPE signal handler";

  DCHECK(ctrl_fd.is_valid());
  CHECK(base::SetNonBlocking(ctrl_fd.get()));

  // Close stdin and stdout. Keep stderr open, so that error messages can still
  // be logged.
  IGNORE_EINTR(close(STDIN_FILENO));
  IGNORE_EINTR(close(STDOUT_FILENO));

  // This loop will only end when either there are no processes left inside
  // our PID namespace or we get a signal.
  int last_failure_code = 0;

  while (true) {
    // Wait for any child process to terminate.
    int wstatus;
    const pid_t pid = HANDLE_EINTR(wait(&wstatus));

    if (pid < 0) {
      if (errno == ECHILD) {
        // No more child. By then, we should have closed the control pipe.
        DCHECK(!ctrl_fd.is_valid());
        VLOG(1) << "The 'init' process is finishing with exit code "
                << last_failure_code;
        return last_failure_code;
      }

      PLOG(FATAL) << "The 'init' process cannot wait for child processes";
    }

    // A child process finished.
    // Convert wait status to exit code.
    const int exit_code = WaitStatusToExitCode(wstatus);
    DCHECK_GE(exit_code, 0);
    VLOG(1) << "Child process " << pid
            << " of the 'init' process finished with exit code" << exit_code;

    if (exit_code > 0)
      last_failure_code = exit_code;

    // Was it the 'launcher' process?
    if (pid != launcher_pid)
      continue;

    // Write the 'launcher' process's exit code to the control pipe.
    DCHECK(ctrl_fd.is_valid());
    const ssize_t written =
        HANDLE_EINTR(write(ctrl_fd.get(), &exit_code, sizeof(exit_code)));
    if (written != sizeof(exit_code)) {
      PLOG(FATAL) << "Cannot write exit code " << exit_code
                  << " of the 'launcher' process " << launcher_pid
                  << " to control pipe " << ctrl_fd.get();
    }

    // Close the control pipe.
    ctrl_fd.reset();
  }
}

pid_t SandboxedInit::StartLauncher(Launcher launcher) {
  const pid_t pid = fork();
  if (pid < 0)
    PLOG(FATAL) << "Cannot fork";

  if (pid > 0)
    return pid;  // In parent process

  // In launcher process.
  // Avoid leaking file descriptor into launcher process.
  DCHECK(ctrl_fd_.is_valid());
  ctrl_fd_.reset();

  // Launch the invoked program.
  _exit(std::move(launcher).Run());
  NOTREACHED();
}

int SandboxedInit::PollLauncher(base::ScopedFD* const ctrl_fd) {
  DCHECK(ctrl_fd);
  DCHECK(ctrl_fd->is_valid());

  int exit_code;
  const ssize_t read_bytes =
      HANDLE_EINTR(read(ctrl_fd->get(), &exit_code, sizeof(exit_code)));

  // If an error occurs while reading from the pipe, consider that the init
  // process was killed before it could even write to the pipe.
  const int error_code = MINIJAIL_ERR_SIG_BASE + SIGKILL;

  if (read_bytes < 0) {
    // Cannot read data from pipe.
    if (errno == EAGAIN) {
      VLOG(1) << "No data is available from control pipe " << ctrl_fd->get()
              << " yet";
      return -1;
    }

    PLOG(ERROR) << "Cannot read from control pipe";
    exit_code = error_code;
  } else if (read_bytes < sizeof(exit_code)) {
    // Cannot read enough data from pipe.
    DCHECK_GE(read_bytes, 0);
    LOG(ERROR) << "Short read of " << read_bytes << " bytes from control pipe "
               << ctrl_fd->get();
    exit_code = error_code;
  } else {
    DCHECK_EQ(read_bytes, sizeof(exit_code));
    VLOG(1) << "Received exit code " << exit_code << " from control pipe "
            << ctrl_fd->get();
    DCHECK_GE(exit_code, 0);
    DCHECK_LE(exit_code, 255);
  }

  ctrl_fd->reset();
  return exit_code;
}

int SandboxedInit::WaitForLauncher(base::ScopedFD* const ctrl_fd) {
  while (true) {
    DCHECK(ctrl_fd);
    DCHECK(ctrl_fd->is_valid());

    struct pollfd pfd = {.fd = ctrl_fd->get(), .events = POLLIN};
    const int n = HANDLE_EINTR(poll(&pfd, 1, /* timeout = */ -1));
    PLOG_IF(ERROR, n < 0) << "Cannot poll control pipe " << pfd.fd;

    if (const int exit_code = PollLauncher(ctrl_fd); exit_code >= 0)
      return exit_code;
  }
}

int SandboxedInit::WaitStatusToExitCode(int wstatus) {
  if (WIFEXITED(wstatus)) {
    return WEXITSTATUS(wstatus);
  }

  if (WIFSIGNALED(wstatus)) {
    // Mirrors behavior of minijail_wait().
    const int signum = WTERMSIG(wstatus);
    return signum == SIGSYS ? MINIJAIL_ERR_JAIL
                            : MINIJAIL_ERR_SIG_BASE + signum;
  }

  return -1;
}

}  // namespace cros_disks
