// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/maitred/init.h"

#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

// These usually need to come after the sys/ includes.
#include <linux/dm-ioctl.h>
#include <linux/loop.h>
#include <linux/vm_sockets.h>

#include <algorithm>
#include <limits>
#include <list>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/message_loop/message_loop.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <grpc++/grpc++.h>

#include "vm_tools/common/constants.h"

#include "vm_host.grpc.pb.h"  // NOLINT(build/include)

using std::string;

namespace vm_tools {
namespace maitred {
namespace {

// Path to the root directory for cgroups.
constexpr char kCgroupRootDir[] = "/sys/fs/cgroup";

// Name of the directory in every cgroup subsystem for dealing with containers.
constexpr char kCgroupContainerSuffix[] = "chronos_containers";

// Default value of the PATH environment variable.
constexpr char kDefaultPath[] = "/usr/bin:/usr/sbin:/bin:/sbin";

// Uid and Gid for the chronos user and group, respectively.
constexpr uid_t kChronosUid = 1000;
constexpr gid_t kChronosGid = 1000;

// Retry threshould and duration for processes that respawn.  If a process needs
// to be respawned more than kMaxRespawnCount times in the last
// kRespawnWindowSeconds, then it will stop being respawned.
constexpr size_t kMaxRespawnCount = 10;
constexpr base::TimeDelta kRespawnWindowSeconds =
    base::TimeDelta::FromSeconds(30);

// Number of seconds that we should wait before force-killing processes for
// shutdown.
constexpr base::TimeDelta kShutdownTimeout = base::TimeDelta::FromSeconds(10);

// Mounts that must be created on boot.
constexpr struct {
  const char* source;
  const char* target;
  const char* fstype;
  unsigned long flags;  // NOLINT(runtime/int)
  const void* data;
  bool failure_is_fatal;  // Abort if this mount fails.
} mounts[] = {
    {
        .source = "proc",
        .target = "/proc",
        .fstype = "proc",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = nullptr,
        .failure_is_fatal = true,
    },
    {
        .source = "sys",
        .target = "/sys",
        .fstype = "sysfs",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = nullptr,
        .failure_is_fatal = true,
    },
    {
        .source = "tmp",
        .target = "/tmp",
        .fstype = "tmpfs",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = nullptr,
        .failure_is_fatal = true,
    },
    {
        .source = "run",
        .target = "/run",
        .fstype = "tmpfs",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "mode=0755",
        .failure_is_fatal = true,
    },
    {
        .source = "shmfs",
        .target = "/dev/shm",
        .fstype = "tmpfs",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = nullptr,
        .failure_is_fatal = true,
    },
    {
        .source = "devpts",
        .target = "/dev/pts",
        .fstype = "devpts",
        .flags = MS_NOSUID | MS_NOEXEC,
        .data = "gid=5,mode=0620,ptmxmode=666",
        .failure_is_fatal = true,
    },
    {
        .source = "var",
        .target = "/var",
        .fstype = "tmpfs",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "mode=0755",
        .failure_is_fatal = true,
    },
    {
        .source = "none",
        .target = kCgroupRootDir,
        .fstype = "tmpfs",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "mode=0755",
        .failure_is_fatal = true,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/cpu,cpuacct",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "cpu,cpuacct",
        .failure_is_fatal = true,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/freezer",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "freezer",
        .failure_is_fatal = true,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/devices",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "devices",
        .failure_is_fatal = true,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/net_cls,net_prio",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "net_cls",
        .failure_is_fatal = false,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/cpuset",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "cpuset",
        .failure_is_fatal = true,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/blkio",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "blkio",
        .failure_is_fatal = false,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/memory",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "memory",
        .failure_is_fatal = false,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/pids",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "pids",
        .failure_is_fatal = false,
    },
    {
        .source = "cgroup",
        .target = "/sys/fs/cgroup/systemd",
        .fstype = "cgroup",
        .flags = MS_NOSUID | MS_NODEV | MS_NOEXEC,
        .data = "none,name=systemd",
        .failure_is_fatal = false,
    },
};

// Symlinks to be created on boot. It's done after all mounts have completed.
constexpr struct {
  const char* source;
  const char* target;
} symlinks[] = {
    {
        .source = "/sys/fs/cgroup/cpu,cpuacct", .target = "/sys/fs/cgroup/cpu",
    },
    {
        .source = "/sys/fs/cgroup/cpu,cpuacct",
        .target = "/sys/fs/cgroup/cpuacct",
    },
    {
        .source = "/sys/fs/cgroup/net_cls,net_prio",
        .target = "/sys/fs/cgroup/net_cls",
    },
};

// Directories to be created on boot.  These are created only after all the
// mounts have completed.
constexpr struct {
  const char* path;
  mode_t mode;
} boot_dirs[] = {
    {
        .path = "/run/lock",
        .mode = 01777,
    },
    {
        .path = "/run/sshd",
        .mode = 01777,
    },
    {
        .path = "/run/tokens",
        .mode = 01777,
    },
    {
        .path = "/var/cache",
        .mode = 0755,
    },
    {
        .path = "/var/db",
        .mode = 0755,
    },
    {
        .path = "/var/empty",
        .mode = 0755,
    },
    {
        .path = "/var/log",
        .mode = 0755,
    },
    {
        .path = "/var/spool",
        .mode = 0755,
    },
    {
        .path = "/var/lib",
        .mode = 0755,
    },
    {
        .path = "/var/lib/lxc",
        .mode = 0755,
    },
    {
        .path = "/var/lib/lxc/rootfs",
        .mode = 0755,
    },
    {
        .path = "/var/lib/misc",
        .mode = 0755,
    },
};

// These limits are based on suggestions from lxd doc/production-setup.md.
constexpr struct {
  uint8_t resource_type;
  rlimit limit;
} resource_limits[] = {
    {
        .resource_type = RLIMIT_NOFILE,
        .limit = {.rlim_cur = 1048576, .rlim_max = 1048576, },
    },
    {
        .resource_type = RLIMIT_MEMLOCK,
        .limit = {.rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY, },
    },
};

constexpr struct {
  const char* path;
  const char* value;
} sysctl_limits[] = {
    {
        .path = "/proc/sys/fs/inotify/max_queued_events", .value = "1048576",
    },
    {
        .path = "/proc/sys/fs/inotify/max_user_instances", .value = "1048576",
    },
    {
        .path = "/proc/sys/fs/inotify/max_user_watches", .value = "1048576",
    },
    {
        .path = "/proc/sys/vm/max_map_count", .value = "262144",
    },
};

// Information about any errors that happen in the child process before the exec
// call.  This is sent back to the parent process via a socket.
struct __attribute__((packed)) ChildErrorInfo {
  enum class Reason : uint8_t {
    // Failed to set session id.
    SESSION_ID = 0,
    // Unable to open console.
    CONSOLE = 1,
    // Unable to set stdio fds.
    STDIO_FD = 2,
    // Unable to set environment variable.
    SETENV = 3,
    // Unable to reset signal handlers.
    SIGNAL_RESET = 4,
    // Failed to exec the requested program.
    EXEC = 5,
  };

  union {
    // If |reason| is STDIO_FD, the fd that we failed to dup.
    int32_t fd;

    // If |reason| is SETENV, then the child process will append the key and
    // value of the environment variable pair that failed to this struct.  This
    // value tells the parent process the length of the 2 strings, including the
    // '\0' byte for each string.
    uint16_t env_length;

    // If |reason| is SIGNAL_RESET, the signal number for which we failed to set
    // the default disposition.
    int32_t signo;
  } details;

  // The errno value after the failed action.
  int32_t err;

  // Error reason.
  Reason reason;
};

// Number of defined signals that the process could receive (not including
// real time signals).
constexpr int kNumSignals = 32;

// Resets all signal handlers to the default.  This is called in child processes
// immediately before exec-ing so that signals are not unexpectedly blocked.
// Returns 0 if all signal handlers were successfully set to their default
// dispositions.  Returns the signal number of the signal for which resetting
// the signal handler failed, if any.  Callers should inspect errno for the
// error.
int ResetSignalHandlers() {
  for (int signo = 1; signo < kNumSignals; ++signo) {
    if (signo == SIGKILL || signo == SIGSTOP) {
      // sigaction returns an error if we try to set the disposition of these
      // signals to SIG_DFL.
      continue;
    }
    struct sigaction act = {
        .sa_handler = SIG_DFL,
        .sa_flags = 0,
    };
    sigemptyset(&act.sa_mask);

    if (sigaction(signo, &act, nullptr) != 0) {
      return signo;
    }
  }

  return 0;
}

// Recursively changes the owner and group for all files and directories in
// |path| (including |path|) to |uid| and |gid|, respectively.
bool ChangeOwnerAndGroup(base::FilePath path, uid_t uid, gid_t gid) {
  base::FileEnumerator enumerator(
      path, true /*recursive*/,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (base::FilePath current = enumerator.Next(); !current.empty();
       current = enumerator.Next()) {
    if (chown(current.value().c_str(), uid, gid) != 0) {
      PLOG(ERROR) << "Failed to change owner and group for " << current.value()
                  << " to " << uid << ":" << gid;
      return false;
    }
  }

  // FileEnumerator doesn't include the root path so change it manually here.
  if (chown(path.value().c_str(), uid, gid) != 0) {
    PLOG(ERROR) << "Failed to change owner and group for " << path.value()
                << " to " << uid << ":" << gid;
    return false;
  }

  return true;
}

// Performs various setup steps in the child process after calling fork() but
// before calling exec(). |error_fd| should be a valid file descriptor for a
// socket and will be used to send error information back to the parent process
// if any of the setup steps fail.
void DoChildSetup(const char* console,
                  const std::map<string, string>& env,
                  int error_fd) {
  // Create a new session and process group.
  if (setsid() == -1) {
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::SESSION_ID,
        .err = errno,
    };

    send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
    _exit(errno);
  }

  // File descriptor for the child's stdio.
  int fd = open(console, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::CONSOLE,
        .err = errno,
    };

    send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
    _exit(errno);
  }

  // Override the parent's stdio fds with the console fd.
  for (int newfd = 0; newfd < 3; ++newfd) {
    if (dup2(fd, newfd) < 0) {
      struct ChildErrorInfo info = {
          .reason = ChildErrorInfo::Reason::STDIO_FD,
          .err = errno,
          .details = {.fd = newfd},
      };

      send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
      _exit(errno);
    }
  }

  // Close the console fd, if necessary.
  if (fd >= 3) {
    close(fd);
  }

  // Set the umask back to a reasonable default.
  umask(0022);

  // Set the environment variables.
  for (const auto& pair : env) {
    if (setenv(pair.first.c_str(), pair.second.c_str(), 1) == 0) {
      continue;
    }

    // Failed to set an environment variable.  Send the error back to the
    // parent process.
    uint16_t env_length = 0;
    if (pair.first.size() + pair.second.size() + 2 <
        std::numeric_limits<uint16_t>::max()) {
      env_length =
          static_cast<uint16_t>(pair.first.size() + pair.second.size() + 2);
    }
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::SETENV,
        .err = errno,
        .details = {.env_length = env_length},
    };
    send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);

    // Also send back the offending (key, value) pair if it's not too long.
    // The pair is sent back in the format: <key>\0<value>\0.
    if (env_length != 0) {
      struct iovec iovs[] = {
          {
              .iov_base =
                  static_cast<void*>(const_cast<char*>(pair.first.data())),
              .iov_len = pair.first.size() + 1,
          },
          {
              .iov_base =
                  static_cast<void*>(const_cast<char*>(pair.second.data())),
              .iov_len = pair.second.size() + 1,
          },
      };
      struct msghdr hdr = {
          .msg_name = nullptr,
          .msg_namelen = 0,
          .msg_iov = iovs,
          .msg_iovlen = sizeof(iovs) / sizeof(iovs[0]),
          .msg_control = nullptr,
          .msg_controllen = 0,
          .msg_flags = 0,
      };
      sendmsg(error_fd, &hdr, MSG_NOSIGNAL);
    }
    _exit(errno);
  }

  // Restore signal handlers and unblock all signals.
  int signo = ResetSignalHandlers();
  if (signo != 0) {
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::SIGNAL_RESET,
        .err = errno,
        .details = {.signo = signo},
    };

    send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
    _exit(errno);
  }

  // Unblock all signals.
  sigset_t mask;
  sigfillset(&mask);
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

// Logs information about the error that occurred in the child process.
void LogChildError(const struct ChildErrorInfo& child_info, int fd) {
  const char* msg = nullptr;
  switch (child_info.reason) {
    case ChildErrorInfo::Reason::SESSION_ID:
      msg = "Failed to set session id in child process: ";
      break;
    case ChildErrorInfo::Reason::CONSOLE:
      msg = "Failed to open console in child process: ";
      break;
    case ChildErrorInfo::Reason::STDIO_FD:
      msg = "Failed to setup stdio file descriptors in child process: ";
      break;
    case ChildErrorInfo::Reason::SETENV:
      msg = "Failed to set environment variable in child process: ";
      break;
    case ChildErrorInfo::Reason::SIGNAL_RESET:
      msg = "Failed to reset signal handler disposition in child process: ";
      break;
    case ChildErrorInfo::Reason::EXEC:
      msg = "Failed to execute requested program in child process: ";
      break;
  }

  LOG(ERROR) << msg << strerror(child_info.err);

  if (child_info.reason == ChildErrorInfo::Reason::STDIO_FD) {
    LOG(ERROR) << "Unable to dup console fd to " << child_info.details.fd;
    return;
  }

  if (child_info.reason == ChildErrorInfo::Reason::SIGNAL_RESET) {
    LOG(ERROR) << "Unable to set signal disposition for signal "
               << child_info.details.signo << " to SIG_DFL";
    return;
  }

  if (child_info.reason == ChildErrorInfo::Reason::SETENV &&
      child_info.details.env_length > 0) {
    auto buf = std::make_unique<char[]>(child_info.details.env_length + 1);
    if (recv(fd, buf.get(), child_info.details.env_length, 0) !=
        child_info.details.env_length) {
      PLOG(ERROR) << "Unable to fetch error details from child process";
      return;
    }
    buf[child_info.details.env_length] = '\0';

    char* key = buf.get();
    char* value = strchr(buf.get(), '\0');
    if (value - key == child_info.details.env_length) {
      LOG(ERROR) << "Missing value in SETENV error details";
      return;
    }

    // Step over the nullptr at the end of |key|.
    ++value;

    LOG(ERROR) << "Unable to set " << key << " to " << value;
  }
}

// Waits for all the processes in |pids| to exit.  Returns when all processes
// have exited or when |deadline| is reached, whichever happens first.
void WaitForChildren(std::set<pid_t> pids, base::Time deadline) {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);

  while (!pids.empty()) {
    // First reap any child processes that have already exited.
    while (true) {
      pid_t child = waitpid(-1, nullptr, WNOHANG);
      if (child < 0 && errno != ECHILD) {
        PLOG(ERROR) << "Failed to wait for child processes";
        return;
      }

      if (child <= 0) {
        // Either there are no more children or they have not exited yet.
        break;
      }

      pids.erase(child);
    }

    // We will not find out about all child processes.  For example some
    // processes might set up custom SIGTERM handlers and then try to handle
    // the termination of their own children, in which case we would not find
    // out about those processes here.
    for (auto iter = pids.begin(); iter != pids.end();) {
      // If the process still exists then leave it in the set.  kill() with a
      // signal value of 0 is explicitly documented as a way to check for the
      // existence of a given process.
      if (kill(*iter, 0) == 0) {
        ++iter;
        continue;
      }

      // If the process has already exited, then remove it from the set.
      DCHECK_EQ(errno, ESRCH);
      iter = pids.erase(iter);
    }

    // If there are no processes left then exit early.  Otherwise we will block
    // for the full timeout duration in the sigtimedwait below.
    if (pids.empty()) {
      return;
    }

    // Check the deadline.
    base::Time now = base::Time::Now();
    if (now >= deadline) {
      return;
    }

    // Wait for more processes to exit.
    struct timespec ts = (deadline - now).ToTimeSpec();
    int ret = sigtimedwait(&mask, nullptr, &ts);
    if (ret == SIGCHLD) {
      // One or more child processes have exited.
      continue;
    }

    if (ret < 0 && errno == EAGAIN) {
      // Deadline expired.
      return;
    }

    if (ret < 0) {
      PLOG(WARNING) << "Unable to wait for processes to exit";
    } else {
      LOG(WARNING) << "Unexpected return value from sigtimedwait(): "
                   << strsignal(ret);
    }
  }

  // Control should never reach here.
  NOTREACHED();
}

// Cached pid of this process.  Starting from version 2.24, glibc stopped
// caching the pid of the current process since the cache interacts in weird
// ways with certain clone() and unshare() flags.  This value is only checked
// and set in ShouldKillProcess().
static pid_t cached_pid = 0;

// Returns true if it is safe to kill |process| either with a SIGTERM or a
// SIGKILL.  |path| must be the path to the process directory in /proc.
bool ShouldKillProcess(pid_t process, const base::FilePath& path) {
  if (cached_pid == 0) {
    cached_pid = getpid();
  }

  if (process == 1 || process == cached_pid) {
    // Probably not a good idea to kill ourselves.
    return false;
  }

  // Get the process's UID.
  uid_t uid = -1;
  string status;
  if (!base::ReadFileToString(path.Append("status"), &status)) {
    PLOG(WARNING) << "Failed to read status for process " << process;

    // Don't send a signal to this process just to be on the safe side.
    return false;
  }

  for (const auto& line : base::SplitStringPiece(
           status, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with("Uid:")) {
      continue;
    }

    std::vector<base::StringPiece> tokens = base::SplitStringPiece(
        line, base::kWhitespaceASCII, base::TRIM_WHITESPACE,
        base::SPLIT_WANT_NONEMPTY);
    DCHECK_EQ(tokens.size(), 5);
    if (!base::StringToUint(tokens[1], &uid)) {
      LOG(WARNING) << "Failed to parse uid (" << tokens[1] << ") for process "
                   << process;
      return false;
    }

    break;
  }

  DCHECK_NE(uid, -1);
  if (uid != 0) {
    // All non-root processes can be killed.
    return true;
  }

  // Check if this is a kernel process.
  char buf;
  if (readlink(path.Append("exe").value().c_str(), &buf, sizeof(buf)) < 0 &&
      errno == ENOENT) {
    // Kernel processes have no executable.
    return false;
  }

  return true;
}

// Broadcast the signal |signo| to all processes.  |signo| must be either
// SIGTERM or SIGKILL.  If |pids| is not nullptr, then it is filled with the
// pids of the processes to which |signo| was successfully sent.
void BroadcastSignal(int signo, std::set<pid_t>* pids) {
  DCHECK(signo == SIGTERM || signo == SIGKILL);

  // We are about to walk the process tree.  Pause all processes so that new
  // processes don't appear or disappear while we're walking the tree.
  // Additionally, pausing all the processes here means that we don't end up
  // with unnecessary thrashing in the system.  For example, consider a
  // pipeline of programs:
  //
  //     cmd1 | cmd2 | cmd3 | cmd4
  //
  // If cmd2 gets killed first, cmd3 might wake up from its read because its
  // pipe is now closed and might end up doing some extra work even though we
  // are going to be killing it very soon as well.  Pausing all processes
  // avoids this problem and ensures that the signal is delivered atomically to
  // all processes.
  if (kill(-1, SIGSTOP) < 0 && errno != ESRCH) {
    PLOG(WARNING) << "Unable to send SIGSTOP to all processes.  System "
                  << "thrashing may occur";
  }

  base::FileEnumerator enumerator(base::FilePath("/proc"),
                                  false /* recursive */,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    pid_t process;
    if (!base::StringToInt(path.BaseName().value(), &process)) {
      // Ignore anything that doesn't look like a pid.
      continue;
    }

    if (!ShouldKillProcess(process, path)) {
      continue;
    }

    if (kill(process, signo) < 0) {
      PLOG(ERROR) << "Failed to send " << strsignal(signo) << " to process "
                  << process;
      continue;
    }

    // Now that we've sent the signal to the process wake it up.  This way we
    // avoid a thundering herd problem if all the processes wake up at the same
    // time later.
    if (kill(process, SIGCONT) < 0 && errno != ESRCH) {
      // It's possible the process is already gone (for example if signo was
      // SIGKILL).  Only log an error if it's not that case.
      PLOG(WARNING) << "Failed to wake up process " << process;
    }

    if (pids) {
      pids->insert(process);
    }
  }

  // Now restart any programs that may still be hanging around.  There shouldn't
  // actually be any but just in case one of the attempts to send SIGCONT
  // earlier failed we can try one more time here.
  if (kill(-1, SIGCONT) < 0 && errno != ESRCH) {
    PLOG(WARNING) << "Unable to send SIGCONT to all processes.  Some "
                  << "processes may still be frozen";
  }
}

// Detaches all loopback devices.
void DetachLoopback() {
  LOG(INFO) << "Detaching loopback devices";

  const base::FilePath kDev("/dev");

  base::FileEnumerator enumerator(
      base::FilePath("/sys/block"), false /*recursive*/,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS,
      "loop*" /*pattern*/);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    const base::FilePath backing_file =
        path.Append("loop").Append("backing_file");
    if (!base::PathExists(backing_file)) {
      continue;
    }

    const base::FilePath dev_path = kDev.Append(path.BaseName());

    LOG(INFO) << "Detaching " << dev_path.value();

    base::ScopedFD loopdev(open(dev_path.value().c_str(), O_RDWR | O_CLOEXEC));
    if (!loopdev.is_valid()) {
      PLOG(ERROR) << "Unable to open " << dev_path.value();
      continue;
    }

    if (ioctl(loopdev.get(), LOOP_CLR_FD, 0) != 0) {
      PLOG(ERROR) << "Failed to remove backing file for /dev/"
                  << path.BaseName().value();
    }
  }
}

// Removes all device mapper devices.
void RemoveDevMapper() {
  LOG(INFO) << "Removing device mapper devices";

  const base::FilePath kDMControl("/dev/mapper/control");

  base::ScopedFD dm_control(
      open(kDMControl.value().c_str(), O_RDWR | O_CLOEXEC));
  if (!dm_control.is_valid()) {
    PLOG(ERROR) << "Failed to open " << kDMControl.value();
    return;
  }

  struct dm_ioctl param = {
      // clang-format off
      .version = {
          DM_VERSION_MAJOR,
          DM_VERSION_MINOR,
          DM_VERSION_PATCHLEVEL,
      },
      // clang-format on
      .data_size = sizeof(struct dm_ioctl),
      .data_start = sizeof(struct dm_ioctl),
      .flags = DM_DEFERRED_REMOVE,
  };
  if (ioctl(dm_control.get(), DM_REMOVE_ALL, &param) != 0) {
    PLOG(ERROR) << "Failed to remove device mapper devices";
  }
}

// Returns true if |mount_point| should not be unmounted even during the
// shutdown sequence.
bool IsProtectedMount(const string& mount_point) {
  const char* const kProtectedMounts[] = {
      "/dev",
      "/proc",
      "/sys",
  };

  if (mount_point == "/") {
    return true;
  }

  for (const char* mount : kProtectedMounts) {
    if (mount == mount_point ||
        base::FilePath(mount).IsParent(base::FilePath(mount_point))) {
      return true;
    }
  }

  return false;
}

// Unmounts all non-essential filesystems.
void UnmountFilesystems() {
  LOG(INFO) << "Unmounting filesystems";

  base::ScopedFILE mountinfo(fopen("/proc/self/mounts", "r"));
  if (!mountinfo) {
    PLOG(ERROR) << "Failed to open /proc/self/mounts";
    return;
  }

  // Parse all the mounts into a vector since we need to unmount them in
  // reverse order.
  std::vector<string> mount_points;
  char buf[1024 + 4];
  struct mntent entry;
  while (getmntent_r(mountinfo.get(), &entry, buf, sizeof(buf)) != nullptr) {
    mount_points.emplace_back(entry.mnt_dir);
  }

  for (auto iter = mount_points.rbegin(), end = mount_points.rend();
       iter != end; ++iter) {
    if (IsProtectedMount(*iter)) {
      continue;
    }

    LOG(INFO) << "Unmounting " << *iter;
    if (umount(iter->c_str()) != 0) {
      PLOG(ERROR) << "Failed to unmount " << *iter;
    }
  }
}

}  // namespace

class Init::Worker : public base::MessageLoopForIO::Watcher {
 public:
  // Relevant information about processes launched by this process.
  struct ChildInfo {
    std::vector<string> argv;
    std::map<string, string> env;
    bool respawn;
    bool use_console;
    bool wait_for_exit;

    std::list<base::Time> spawn_times;
  };

  Worker() : watcher_(FROM_HERE) {}
  ~Worker() = default;

  // Start the worker.  This will set up a signalfd for receiving SIGHCHLD
  // events.
  void Start();

  // Actually spawns a child process.  Waits until it receives confirmation from
  // the child that the requested program was actually started and fills in
  // |launch_info| with information about the process.  Additionally if
  // |info.wait_for_exit| is true, then waits until the child process exits or
  // is killed before returning.
  void Spawn(struct ChildInfo info, int semfd, ProcessLaunchInfo* launch_info);

  // Shuts down the system.  First broadcasts SIGTERM to all processes and
  // waits for those processes to exit up to a deadline.  Then kills any
  // remaining processes with SIGKILL.  |notify_fd| must be an eventfd, which
  // is notified after all processes are killed.
  void Shutdown(int notify_fd);

  // base::MessageLoopForIO::Watcher overrides.
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

 private:
  // File descriptor on which we will receive SIGCHLD events.
  base::ScopedFD signal_fd_;
  base::MessageLoopForIO::FileDescriptorWatcher watcher_;

  // Information about processes launched by this process.
  std::map<pid_t, ChildInfo> children_;

  DISALLOW_COPY_AND_ASSIGN(Worker);
};

void Init::Worker::Start() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);

  // Block SIGCHLD so that we can get it via the signalfd.
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
    PLOG(ERROR) << "Failed to block SIGCHLD";
  }

  signal_fd_.reset(signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
  PCHECK(signal_fd_.is_valid()) << "Unable to create signal fd";

  bool ret = base::MessageLoopForIO::current()->WatchFileDescriptor(
      signal_fd_.get(), true /*persistent*/, base::MessageLoopForIO::WATCH_READ,
      &watcher_, this);

  CHECK(ret) << "Failed to watch SIGHCHLD file descriptor";
}

void Init::Worker::Spawn(struct ChildInfo info,
                         int semfd,
                         ProcessLaunchInfo* launch_info) {
  DCHECK_GT(info.argv.size(), 0);
  DCHECK(launch_info);

  // Build the argv.
  std::vector<const char*> argv(info.argv.size());
  std::transform(info.argv.begin(), info.argv.end(), argv.begin(),
                 [](const string& arg) -> const char* { return arg.c_str(); });
  argv.emplace_back(nullptr);

  // Create a pair of sockets for communicating information about the child
  // process setup.  If there was an error in any of the steps performed before
  // running execvp, then the child process will send back a ChildErrorInfo
  // struct with the error details over the socket.  If the execvp runs
  // successful then the socket will automatically be closed (because of
  // the SOCK_CLOEXEC flag) and the parent will read 0 bytes from its end
  // of the socketpair.
  int info_fds[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, info_fds) != 0) {
    PLOG(ERROR) << "Failed to create socketpair for child process";
    launch_info->status = ProcessStatus::FAILED;
    return;
  }

  // Block all signals before forking to prevent signals from arriving in the
  // child.
  sigset_t mask, omask;
  sigfillset(&mask);
  sigprocmask(SIG_BLOCK, &mask, &omask);

  pid_t pid = fork();
  if (pid < 0) {
    PLOG(ERROR) << "Failed to fork";
    launch_info->status = ProcessStatus::FAILED;
    return;
  }

  if (pid == 0) {
    // Child process.
    close(info_fds[0]);

    const char* console = info.use_console ? "/dev/console" : "/dev/null";
    DoChildSetup(console, info.env, info_fds[1]);

    // Launch the process.
    execvp(argv[0], const_cast<char* const*>(argv.data()));

    // execvp never returns except in case of an error.
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::EXEC,
        .err = errno,
    };

    send(info_fds[1], &info, sizeof(info), MSG_NOSIGNAL);
    _exit(errno);
  }

  // Parent process.
  close(info_fds[1]);
  struct ChildErrorInfo child_info = {};
  ssize_t ret = recv(info_fds[0], &child_info, sizeof(child_info), 0);

  // There are 3 possibilities here:
  //   - The process setup completed successfully and the program was launched.
  //     In this case the socket fd in the child process will be closed on
  //     exec and ret will be 0.
  //   - An error occurred during setup.  ret will be sizeof(child_info).
  //   - An error occurred during the recv.  In this case we assume the child
  //     setup was successful.  If it wasn't, we'll find out about it through
  //     the normal child reaping mechanism.
  if (ret == sizeof(child_info)) {
    // Error occurred in the child.
    LogChildError(child_info, info_fds[0]);

    // Reap the child process here since we know it already failed.
    int status = 0;
    pid_t child = waitpid(pid, &status, 0);
    DCHECK_EQ(child, pid);

    launch_info->status = ProcessStatus::FAILED;
  } else if (ret < 0) {
    PLOG(ERROR) << "Failed to receive information about child process setup";
    launch_info->status = ProcessStatus::UNKNOWN;
  }
  close(info_fds[0]);

  if (ret == 0 && info.wait_for_exit) {
    int status = 0;
    pid_t child = waitpid(pid, &status, 0);
    DCHECK_EQ(child, pid);

    if (WIFEXITED(status)) {
      launch_info->status = ProcessStatus::EXITED;
      launch_info->code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      launch_info->status = ProcessStatus::SIGNALED;
      launch_info->code = WTERMSIG(status);
    } else {
      launch_info->status = ProcessStatus::UNKNOWN;
    }

  } else if (ret == 0) {
    info.spawn_times.emplace_back(base::Time::Now());

    // result is a pair<iterator, bool>.
    auto result = children_.emplace(pid, std::move(info));
    DCHECK(result.second);

    launch_info->status = ProcessStatus::LAUNCHED;
  }

  if (semfd != -1) {
    uint64_t done = 1;
    ssize_t count = write(semfd, &done, sizeof(done));
    DCHECK_EQ(count, sizeof(done));
  }

  // Restore the signal mask.
  sigprocmask(SIG_SETMASK, &omask, nullptr);
}

void Init::Worker::Shutdown(int notify_fd) {
  DCHECK_NE(notify_fd, -1);

  // Stop watching for SIGCHLD.  We will do it manually here.
  watcher_.StopWatchingFileDescriptor();
  signal_fd_.reset();

  // First send SIGTERM to all processes.
  std::set<pid_t> pids;
  BroadcastSignal(SIGTERM, &pids);

  // Wait for those processes to terminate.
  WaitForChildren(std::move(pids), base::Time::Now() + kShutdownTimeout);

  // Kill anything left with SIGKILL.
  BroadcastSignal(SIGKILL, nullptr);

  // Detach loopback devices.
  DetachLoopback();

  // Remove any device-mapper devices.
  RemoveDevMapper();

  // Unmount all non-essential file systems.
  UnmountFilesystems();

  // Final sync to flush anything left.
  sync();

  // Signal the waiter.
  uint64_t done = 1;
  if (write(notify_fd, &done, sizeof(done)) != sizeof(done)) {
    PLOG(ERROR) << "Failed to wake up shutdown waiter";
  }
}

void Init::Worker::OnFileCanReadWithoutBlocking(int fd) {
  DCHECK_EQ(fd, signal_fd_.get());

  // Pull information about the signal sender out of the fd to ack the signal.
  struct signalfd_siginfo siginfo;
  if (HANDLE_EINTR(read(signal_fd_.get(), &siginfo, sizeof(siginfo))) !=
      sizeof(siginfo)) {
    PLOG(ERROR) << "Failed to read from signalfd";
    return;
  }
  DCHECK_EQ(siginfo.ssi_signo, SIGCHLD);

  // We can't just rely on the information in the siginfo structure because
  // more than one child may have exited but only one SIGCHLD will be
  // generated.
  while (true) {
    int status;
    pid_t pid = waitpid(-1, &status, WNOHANG);
    if (pid <= 0) {
      if (pid == -1) {
        PLOG(ERROR) << "Unable to reap child processes";
      }
      break;
    }

    // See if this is a process we launched.
    struct ChildInfo info = {};
    auto iter = children_.find(pid);
    if (iter != children_.end()) {
      info = std::move(iter->second);
      children_.erase(iter);
    }

    if (WIFEXITED(status)) {
      LOG(INFO) << (info.argv.size() == 0 ? "<unknown process>"
                                          : info.argv[0].c_str())
                << " (" << pid << ") exited with status "
                << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      LOG(INFO) << (info.argv.size() == 0 ? "<unknown process>"
                                          : info.argv[0].c_str())
                << " (" << pid << ") killed by signal " << WTERMSIG(status)
                << (WCOREDUMP(status) ? " (core dumped)" : "");
    } else {
      LOG(WARNING) << "Unknown exit status " << status << " for process "
                   << pid;
    }

    // TODO(jkardatzke): REMOVE THIS UGLY HACK
    // This is a TEMPORARY hack to detect failure in starting a container so
    // that we can send a message back to the host to notify it. Long term the
    // plan is to use the LXD REST API, and once we do that we can remove this.
    if (!info.wait_for_exit && info.argv.size() > 0 &&
        info.argv[0] == "/sbin/minijail0" &&
        (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status)))) {
      // There was a failure in the script, it exited with a signal or a failure
      // code. We don't need to log the details of it because we already did
      // that above.

      // Extract the container name parameter.
      bool token_next = false;
      string container_name;
      for (const auto& arg : info.argv) {
        if (token_next) {
          container_name = arg;
          break;
        }
        if (arg == "--container_name") {
          token_next = true;
        }
      }
      LOG(ERROR) << "Detected failure in run_container.sh execution for "
                 << "container " << container_name;
      vm_tools::StartupListener::Stub stub(grpc::CreateChannel(
          base::StringPrintf("vsock:%u:%u", VMADDR_CID_HOST,
                             vm_tools::kStartupListenerPort),
          grpc::InsecureChannelCredentials()));
      grpc::ClientContext ctx;
      vm_tools::EmptyMessage empty;
      vm_tools::ContainerName container_id;
      container_id.set_name(container_name);
      grpc::Status status =
          stub.ContainerStartupFailed(&ctx, container_id, &empty);
      if (!status.ok()) {
        LOG(WARNING) << "Failed to notify host system that container startup "
                     << "failed: " << status.error_message();
      }
    }

    if (!info.respawn) {
      continue;
    }

    // The process needs to be respawned.  First remove any spawn times older
    // than the respawn counter window.
    base::Time now = base::Time::Now();
    while (info.spawn_times.size() > 0 &&
           now - info.spawn_times.front() > kRespawnWindowSeconds) {
      info.spawn_times.pop_front();
    }

    // Check if the process has respawned too often.
    if (info.spawn_times.size() >= kMaxRespawnCount) {
      LOG(WARNING) << info.argv[0] << " respawning too frequently; stopped";
      continue;
    }

    // Respawn the process.
    LOG(INFO) << "Restarting " << info.argv[0];
    string app(info.argv[0]);

    Init::ProcessLaunchInfo launch_info;
    Spawn(std::move(info), -1, &launch_info);
    switch (launch_info.status) {
      case ProcessStatus::UNKNOWN:
        LOG(WARNING) << app << " has unknown status";
        break;
      case ProcessStatus::EXITED:
        LOG(WARNING) << app << " unexpectedly exited with status "
                     << launch_info.code << ";  stopped";
        break;
      case ProcessStatus::SIGNALED:
        LOG(WARNING) << app << " unexpectedly killed by signal "
                     << launch_info.code << "; stopped";
        break;
      case ProcessStatus::LAUNCHED:
        LOG(INFO) << app << " restarted";
        break;
      case ProcessStatus::FAILED:
        LOG(ERROR) << "Failed to start " << app;
        break;
    }
  }
}

void Init::Worker::OnFileCanWriteWithoutBlocking(int fd) {
  NOTREACHED();
}

std::unique_ptr<Init> Init::Create() {
  auto init = base::WrapUnique<Init>(new Init());

  if (!init->Setup()) {
    init.reset();
  }

  return init;
}

Init::~Init() {
  if (worker_) {
    // worker_ is created after worker_thread_ is started so we don't need to
    // check if it is running.
    worker_thread_.task_runner()->DeleteSoon(FROM_HERE, worker_.release());
  }
}

bool Init::Spawn(std::vector<string> argv,
                 std::map<string, string> env,
                 bool respawn,
                 bool use_console,
                 bool wait_for_exit,
                 ProcessLaunchInfo* launch_info) {
  CHECK(!argv.empty());
  CHECK(!(respawn && wait_for_exit));
  CHECK(launch_info);

  if (!worker_) {
    // If there's no worker then we are currently in the process of shutting
    // down.
    return false;
  }

  struct Worker::ChildInfo info = {.argv = std::move(argv),
                                   .env = std::move(env),
                                   .respawn = respawn,
                                   .use_console = use_console,
                                   .wait_for_exit = wait_for_exit};

  // Create a semaphore that we will use to wait for the worker thread to launch
  // the process and fill in the the ProcessLaunchInfo struct with the result.
  base::ScopedFD sem(eventfd(0 /*initval*/, EFD_CLOEXEC | EFD_SEMAPHORE));
  if (!sem.is_valid()) {
    PLOG(ERROR) << "Failed to create semaphore eventfd";
    return false;
  }

  bool ret = worker_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&Worker::Spawn, base::Unretained(worker_.get()),
                 base::Passed(std::move(info)), sem.get(), launch_info));
  if (!ret) {
    return false;
  }

  uint64_t done = 0;
  ssize_t count = HANDLE_EINTR(read(sem.get(), &done, sizeof(done)));
  DCHECK_EQ(count, sizeof(done));
  DCHECK_EQ(done, 1);

  return true;
}

void Init::Shutdown() {
  base::ScopedFD notify_fd(eventfd(0 /*initval*/, EFD_CLOEXEC | EFD_SEMAPHORE));
  if (!notify_fd.is_valid()) {
    PLOG(ERROR) << "Failed to create eventfd";
    return;
  }

  bool ret = worker_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&Worker::Shutdown, base::Unretained(worker_.get()),
                            notify_fd.get()));
  if (!ret) {
    LOG(ERROR) << "Failed to post task to worker thread";
    return;
  }

  uint64_t done = 0;
  if (read(notify_fd.get(), &done, sizeof(done)) != sizeof(done)) {
    PLOG(ERROR) << "Failed to read from eventfd";
    return;
  }
  DCHECK_EQ(done, 1);
}

bool Init::SetupResourceLimit() {
  // Setup rlimit.
  for (const auto& rlimit : resource_limits) {
    if (setrlimit(rlimit.resource_type, &rlimit.limit) != 0) {
      PLOG(ERROR) << "Failed to set limit for resouce type: "
                  << rlimit.resource_type;
      return false;
    }
  }

  // Setup sysctl limits.
  for (const auto& syslimit : sysctl_limits) {
    base::ScopedFD sysctl_node(open(syslimit.path, O_RDWR | O_CLOEXEC));

    if (!sysctl_node.is_valid()) {
      PLOG(ERROR) << "Unable to open sysctl node: " << syslimit.path;
      return false;
    }

    ssize_t count =
        write(sysctl_node.get(), syslimit.value, strlen(syslimit.value));
    if (count != strlen(syslimit.value)) {
      PLOG(ERROR) << "Faile to write sysctl node: " << syslimit.path;
      return false;
    }
  }
  return true;
}

bool Init::Setup() {
  // Set the umask properly or the directory modes will not work.
  umask(0000);

  // Do all the mounts.
  for (const auto& mt : mounts) {
    if (mkdir(mt.target, 0755) != 0 && errno != EEXIST) {
      PLOG(ERROR) << "Failed to create " << mt.target;
      if (mt.failure_is_fatal)
        return false;
    }

    if (mount(mt.source, mt.target, mt.fstype, mt.flags, mt.data) != 0) {
      rmdir(mt.target);
      PLOG(ERROR) << "Failed to mount " << mt.target;
      if (mt.failure_is_fatal)
        return false;
    }
  }

  // Setup the resource limits.
  if (!SetupResourceLimit()) {
    return false;
  }

  // Create all the symlinks.
  for (const auto& sl : symlinks) {
    if (symlink(sl.source, sl.target) != 0) {
      PLOG(ERROR) << "Failed to create symlink: source " << sl.source
                  << ", target " << sl.target;
      return false;
    }
  }

  // Create all the directories.
  for (const auto& dir : boot_dirs) {
    if (mkdir(dir.path, dir.mode) != 0 && errno != EEXIST) {
      PLOG(ERROR) << "Failed to create " << dir.path;
      return false;
    }
  }

  // Change the ownership of the kCgroupContainerSuffix directory in each cgroup
  // subsystem to "chronos".
  base::FileEnumerator enumerator(base::FilePath(kCgroupRootDir),
                                  false /*recursive*/,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath current = enumerator.Next(); !current.empty();
       current = enumerator.Next()) {
    base::FilePath target_cgroup = current.Append(kCgroupContainerSuffix);
    if (mkdir(target_cgroup.value().c_str(), 0755) != 0 && errno != EEXIST) {
      PLOG(ERROR) << "Failed to create cgroup " << target_cgroup.value();
      return false;
    }
    if (!ChangeOwnerAndGroup(target_cgroup, kChronosUid, kChronosGid)) {
      return false;
    }
  }

  // Create and setup the container cpusets with the default settings (all cpus,
  // all mems).
  const char* sets[] = {"cpuset.cpus", "cpuset.mems"};
  base::FilePath root_dir = base::FilePath(kCgroupRootDir).Append("cpuset");
  base::FilePath chronos_dir = root_dir.Append(kCgroupContainerSuffix);
  for (const char* set : sets) {
    string contents;
    if (!base::ReadFileToString(root_dir.Append(set), &contents)) {
      PLOG(ERROR) << "Failed to read contents from "
                  << root_dir.Append(set).value();
      return false;
    }

    if (base::WriteFile(chronos_dir.Append(set), contents.c_str(),
                        contents.length()) != contents.length()) {
      PLOG(ERROR) << "Failed to write cpuset contents to "
                  << chronos_dir.Append(set).value();
      return false;
    }
  }

  // Become the session leader.
  if (setsid() == -1) {
    PLOG(ERROR) << "Failed to become session leader";
    return false;
  }

  // Set the controlling terminal.
  if (ioctl(STDIN_FILENO, TIOCSCTTY, 1) != 0) {
    PLOG(ERROR) << "Failed to set controlling terminal";
    return false;
  }

  // Setup up PATH.
  if (clearenv() != 0) {
    PLOG(ERROR) << "Failed to clear environment";
    return false;
  }
  if (setenv("PATH", kDefaultPath, 1 /*overwrite*/) != 0) {
    PLOG(ERROR) << "Failed to set PATH";
    return false;
  }

  // Block SIGCHLD here because we want to handle it in the worker thread.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
    PLOG(ERROR) << "Failed to block SIGCHLD";
    return false;
  }

  // Start the worker.
  base::Thread::Options opts(base::MessageLoop::TYPE_IO, 0 /*stack_size*/);
  if (!worker_thread_.StartWithOptions(opts)) {
    LOG(ERROR) << "Failed to start worker thread";
    return false;
  }

  worker_ = std::make_unique<Worker>();
  bool ret = worker_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&Worker::Start, base::Unretained(worker_.get())));
  if (!ret) {
    LOG(ERROR) << "Failed to post task to worker thread";
    return false;
  }

  // Applications that should be started for every VM.
  struct {
    const char* doc;
    std::vector<string> argv;
    std::map<string, string> env;
    bool respawn;
    bool use_console;
    bool wait_for_exit;
  } startup_applications[] = {
      {
          .doc = "system log collector",
          .argv = {"vm_syslog"},
          .env = {},
          .respawn = true,
          .use_console = false,
          .wait_for_exit = false,
      },
      {
          .doc = "vsock remote shell daemon",
          .argv = {"vshd"},
          .env = {},
          .respawn = true,
          .use_console = false,
          .wait_for_exit = false,
      },
  };

  // Spawn all the startup applications.
  for (auto& app : startup_applications) {
    CHECK(!app.argv.empty());

    LOG(INFO) << "Starting " << app.doc;

    ProcessLaunchInfo info;
    if (!Spawn(std::move(app.argv), std::move(app.env), app.respawn,
               app.use_console, app.wait_for_exit, &info)) {
      LOG(ERROR) << "Unable to launch " << app.doc;
      continue;
    }

    switch (info.status) {
      case ProcessStatus::UNKNOWN:
        LOG(WARNING) << app.doc << " has unknown status";
        break;
      case ProcessStatus::EXITED:
        LOG(INFO) << app.doc << " exited with status " << info.code;
        break;
      case ProcessStatus::SIGNALED:
        LOG(INFO) << app.doc << " killed by signal " << info.code;
        break;
      case ProcessStatus::LAUNCHED:
        LOG(INFO) << app.doc << " started";
        break;
      case ProcessStatus::FAILED:
        LOG(ERROR) << "Failed to start " << app.doc;
        break;
    }
  }

  return true;
}

}  // namespace maitred
}  // namespace vm_tools
