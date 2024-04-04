// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/crosh_shell_tool.h"

#include "debugd/src/error_utils.h"

#include <algorithm>
#include <fcntl.h>
#include <poll.h>
#include <span>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/userdb_utils.h>

namespace debugd {

namespace {

constexpr char kMiniJail[] = "/sbin/minijail0";
constexpr char kMountNsPath[] = "/proc/1/ns/mnt";
constexpr char kShShell[] = "/bin/sh";
constexpr char kBashShell[] = "/bin/bash";
constexpr char kChronosUser[] = "chronos";
// Error-related constants.
constexpr char kCroshDupErrorString[] =
    "Duplicate fd failed for crosh shell process";
constexpr char kCroshIoctlErrorString[] =
    "ioctl failed for crosh shell process";
constexpr char kCroshSyscallErrorString[] = "failed for crosh shell process";
constexpr char kCroshToolErrorString[] = "org.chromium.debugd.error.CroshShell";
constexpr char kCroshWriteErrorString[] =
    "Write fd failed for crosh shell process";

const size_t kBufSize = 4096;
const base::TimeDelta kReapDelay = base::Seconds(1);

bool BashShellAvailable() {
  return base::PathExists(base::FilePath(kBashShell));
}

void SetOwnerToChronos(const base::ScopedFD& fd) {
  uid_t uid;
  gid_t gid;
  if (!brillo::userdb::GetUserInfo(kChronosUser, &uid, &gid)) {
    PLOG(ERROR) << "Unable to look up chronos user info";
    exit(1);
  }

  if (fchown(fd.get(), uid, gid) != 0) {
    PLOG(ERROR) << "fchown " << kCroshSyscallErrorString;
    exit(1);
  }
}

void UpdateWindowSize(const base::ScopedFD& infd,
                      const base::ScopedFD& primary_fd) {
  // Get the window size with TIOCGWINSZ, then set it with TIOCSWINSZ
  // ioctl() calls so that the shell can resize child programs.
  winsize window_size;
  if (ioctl(infd.get(), TIOCGWINSZ, &window_size) != 0) {
    PLOG(ERROR) << kCroshIoctlErrorString;
    exit(1);
  }
  if (ioctl(primary_fd.get(), TIOCSWINSZ, &window_size) != 0) {
    PLOG(ERROR) << kCroshIoctlErrorString;
    exit(1);
  }
}

// Sets up a pseudo terminal and exec’s into a shell process.
void SetUpPseudoTerminal(const base::ScopedFD& shell_lifeline_fd,
                         const base::ScopedFD& infd,
                         const base::ScopedFD& eventfd) {
  if (!shell_lifeline_fd.is_valid()) {
    PLOG(ERROR) << "Invalid lifeline fd provided";
    exit(1);
  }
  const int fd = infd.get();
  if (fd < 0) {
    PLOG(ERROR) << "Invalid fd for crosh shell process";
    exit(1);
  }

  struct termios attrs, oattrs;
  if (tcgetattr(fd, &attrs) != 0) {
    PLOG(ERROR) << "tcgetattr " << kCroshSyscallErrorString;
    exit(1);
  }
  oattrs = attrs;
  cfmakeraw(&attrs);
  if (tcsetattr(fd, TCSANOW, &attrs) != 0) {
    PLOG(ERROR) << "tcsetattr " << kCroshSyscallErrorString;
    exit(1);
  }

  base::ScopedFD scoped_primary_fd(posix_openpt(O_RDWR | O_NOCTTY));
  if (!scoped_primary_fd.is_valid()) {
    PLOG(ERROR) << "posix_openpt " << kCroshSyscallErrorString;
    exit(1);
  }
  SetOwnerToChronos(scoped_primary_fd);
  const int primary_fd = scoped_primary_fd.get();
  if (grantpt(primary_fd) != 0) {
    PLOG(ERROR) << "grantpt " << kCroshSyscallErrorString;
    exit(1);
  }
  if (unlockpt(primary_fd) != 0) {
    PLOG(ERROR) << "unlockpt " << kCroshSyscallErrorString;
    exit(1);
  }

  UpdateWindowSize(infd, scoped_primary_fd);

  const char* primary_name = ptsname(primary_fd);
  if (primary_name == nullptr) {
    PLOG(ERROR) << "ptsname " << kCroshSyscallErrorString;
    exit(1);
  }
  // Don't set O_CLOEXEC, because we want the child process to use this fd.
  base::ScopedFD scoped_subordinate_fd(open(primary_name, O_RDWR | O_NOCTTY));
  if (!scoped_subordinate_fd.is_valid()) {
    PLOG(ERROR) << "open " << kCroshSyscallErrorString;
    exit(1);
  }
  SetOwnerToChronos(scoped_subordinate_fd);
  const int subordinate_fd = scoped_subordinate_fd.get();

  // Fork so the shell can be run with a pseudo terminal.
  pid_t pid = fork();
  switch (pid) {
    case -1:
      PLOG(ERROR) << "fork " << kCroshSyscallErrorString;
      exit(1);
    case 0:
      // Child.
      if (prctl(PR_SET_PDEATHSIG, SIGTERM)) {
        PLOG(ERROR) << "prctl " << kCroshSyscallErrorString;
        exit(1);
      }

      // Dup the lifeline FD, because otherwise ScopedFD will close the only
      // copy of this pipe and the read end will give an error before the shell
      // has exited.
      if (HANDLE_EINTR(dup(shell_lifeline_fd.get())) < 0) {
        PLOG(ERROR) << kCroshDupErrorString;
        exit(1);
      }

      // We need to call setsid(), and prevent Minijail from calling setsid(),
      // in order for the shell to have its own session and job control.
      if (setsid() < 0) {
        PLOG(ERROR) << "setsid " << kCroshSyscallErrorString;
        exit(1);
      }
      if (ioctl(subordinate_fd, TIOCSCTTY, 0) != 0) {
        PLOG(ERROR) << kCroshIoctlErrorString;
        exit(1);
      }
      for (int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        if (HANDLE_EINTR(dup2(subordinate_fd, fd)) < 0) {
          PLOG(ERROR) << kCroshDupErrorString;
          exit(1);
        }
      }
      scoped_primary_fd.reset();
      scoped_subordinate_fd.reset();
      // Sandbox options are similar to those to launch Chrome from the
      // login_manager service, but new_privs are allowed.
      if (execl(kMiniJail, kMiniJail, "--no-new-sessions", "-v", "-V",
                kMountNsPath, "-u", kChronosUser, "-g", kChronosUser, "-G",
                "--no-default-runtime-environment",
                BashShellAvailable() ? kBashShell : kShShell, NULL) != 0) {
        PLOG(ERROR) << "execl " << kCroshSyscallErrorString;
        exit(1);
      }
      break;
  }

  // Parent.
  scoped_subordinate_fd.reset();
  // Handle pseudo terminal IO.
  // TODO(b/323557951): determine if there’s a better way to do this with
  // splice().
  uint8_t buf[kBufSize];
  ssize_t ret;
  fd_set set;
  FD_ZERO(&set);
  while (true) {
    FD_SET(fd, &set);
    FD_SET(primary_fd, &set);
    FD_SET(eventfd.get(), &set);
    int max_fd = std::max({fd, primary_fd, eventfd.get()});
    if (select(max_fd + 1, &set, nullptr, nullptr, nullptr) < 0)
      break;

    if (FD_ISSET(fd, &set)) {
      ret = read(fd, buf, sizeof(buf));
      if (ret < 0)
        break;
      if (ret > 0) {
        if (!base::WriteFileDescriptor(primary_fd, std::span(buf, ret))) {
          PLOG(ERROR) << kCroshWriteErrorString;
          exit(1);
        }
      }
    }
    if (FD_ISSET(primary_fd, &set)) {
      ret = read(primary_fd, buf, sizeof(buf));
      if (ret < 0)
        break;
      if (ret > 0) {
        if (!base::WriteFileDescriptor(fd, std::span(buf, ret))) {
          PLOG(ERROR) << kCroshWriteErrorString;
          exit(1);
        }
      }
    }
    if (FD_ISSET(eventfd.get(), &set)) {
      // Read the eventfd to reset the counter.
      uint64_t counter;
      ret = read(eventfd.get(), &counter, sizeof(counter));
      if (ret < 0)
        break;
      UpdateWindowSize(infd, scoped_primary_fd);
    }
  }

  if (tcsetattr(fd, TCSANOW, &oattrs)) {
    PLOG(ERROR) << "tcsetattr " << kCroshSyscallErrorString;
    exit(1);
  }
  if (waitpid(pid, nullptr, 0) != 0) {
    PLOG(ERROR) << "waitpid " << kCroshSyscallErrorString;
    exit(1);
  }
}

void ReapChildProcess(const pid_t pid) {
  int status;

  if (waitpid(pid, &status, WNOHANG) < 0) {
    PLOG(ERROR) << "waitpid " << kCroshSyscallErrorString;
    // Don't continue trying to reap if waitpid() fails.
    return;
  }

  if (!WIFEXITED(status))
    // Only check !WIFEXITED(status), because checking !WIFSIGNALED(status) too
    // would cause the message loop to exit too early.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ReapChildProcess, pid), kReapDelay);
}

}  // namespace

bool CroshShellTool::Run(const base::ScopedFD& shell_lifeline_fd,
                         const base::ScopedFD& infd,
                         const base::ScopedFD& eventfd,
                         std::string* out_id,
                         brillo::ErrorPtr* error) {
  // Fork so the D-Bus call can return.
  const pid_t pid = fork();
  switch (pid) {
    case -1:
      DEBUGD_ADD_ERROR(error, kCroshToolErrorString,
                       "Could not fork() to create crosh shell process");
      return false;
    case 0:
      // Child.

      // SetUpPseudoTerminal() will exit() for error conditions.
      SetUpPseudoTerminal(shell_lifeline_fd, infd, eventfd);
      break;
    default:
      // Parent.

      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, base::BindOnce(&ReapChildProcess, pid), kReapDelay);
  }

  return true;
}

}  // namespace debugd
