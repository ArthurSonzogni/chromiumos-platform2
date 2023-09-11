// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/minijailed_process_runner.h"

#include <linux/capability.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include <base/check.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/process/process.h>

namespace patchpanel {

namespace {

constexpr char kUnprivilegedUser[] = "nobody";
constexpr uint64_t kModprobeCapMask = CAP_TO_MASK(CAP_SYS_MODULE);
constexpr uint64_t kNetRawCapMask = CAP_TO_MASK(CAP_NET_RAW);
constexpr uint64_t kNetAdminCapMask = CAP_TO_MASK(CAP_NET_ADMIN);
constexpr uint64_t kNetRawAdminCapMask =
    CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);

// `ip netns` needs CAP_SYS_ADMIN for mount(), and CAP_SYS_PTRACE for accessing
// `/proc/${pid}/ns/net` of other processes.
constexpr uint64_t kIpNetnsCapMask =
    CAP_TO_MASK(CAP_SYS_PTRACE) | CAP_TO_MASK(CAP_SYS_ADMIN);

// These match what is used in iptables.cc in firewalld.
constexpr char kIpPath[] = "/bin/ip";
constexpr char kIptablesPath[] = "/sbin/iptables";
constexpr char kIp6tablesPath[] = "/sbin/ip6tables";
constexpr char kModprobePath[] = "/sbin/modprobe";
constexpr char kConntrackPath[] = "/usr/sbin/conntrack";

constexpr char kIptablesSeccompFilterPath[] =
    "/usr/share/policy/iptables-seccomp.policy";

// Used in HandlePollEvent() for poll(). Negative fds will be ignored by poll().
constexpr int kInvalidFd = -1;

// Consume poll() POLLIN event and append the read() result to |output_str| if
// it's not nullptr. On POLLHUP or failure, reset the fd in |pollfd_struct|
// (i.e., set it to a negative value) to exclude it from the next poll(). If
// |output_str| is nullptr, the read() result will just be discarded.
bool HandlePollEvent(struct pollfd* pollfd_struct, std::string* output_str) {
  // Static buffer to avoid it getting allocated on stack every time.
  static constexpr int kBufSize = 4096;
  static char buf[kBufSize] = {0};

  // No event means the function is triggered by a timeout.
  if (pollfd_struct->revents == 0) {
    return true;
  }

  // Only POLLHUP means the writer side has closed the pipe and there is no
  // remaining data to consume.
  if (pollfd_struct->revents == POLLHUP) {
    pollfd_struct->fd = kInvalidFd;
    return true;
  }

  // Other signal other than POLLIN and POLLHUP indicates an error. Note that
  // POLLIN and POLLHUP can be set at the same time.
  if (!(pollfd_struct->revents & POLLIN)) {
    PLOG(ERROR) << "poll() failed, revent=" << pollfd_struct->revents;
    // Note that when the fd field is negative, poll() will ignore the events
    // field and reset the revents field to zero when return, so we don't need
    // to clear other fields here. See `man 2 poll` for details.
    pollfd_struct->fd = kInvalidFd;
    return false;
  }

  ssize_t cnt = HANDLE_EINTR(read(pollfd_struct->fd, buf, kBufSize));
  if (cnt == -1) {
    PLOG(ERROR) << "read() failed";
    pollfd_struct->fd = kInvalidFd;
    return false;
  }

  if (output_str) {
    output_str->append({buf, static_cast<size_t>(cnt)});
  }

  return true;
}

// Reads the pipes of stdout and stderr from a child process, until the write
// sides of both peers are closed, which is a signal that the child process is
// exiting. If |deadline| is set, this function will return no matter if the
// pipes are closed when the deadline is reached. Returns whether the pipes have
// been closed, i.e., returns false if the timeout happened, and true otherwise.
bool ReadPipesUntilClose(std::string_view logging_tag,
                         int fd_stdout,
                         int fd_stderr,
                         std::optional<base::Time> deadline,
                         std::string* str_stdout,
                         std::string* str_stderr) {
  struct pollfd pollfds[] = {
      {.fd = fd_stdout, .events = POLLIN},
      {.fd = fd_stderr, .events = POLLIN},
  };

  static constexpr auto kDefaultPollInterval = base::Milliseconds(500);
  while (1) {
    base::TimeDelta poll_interval = kDefaultPollInterval;
    if (deadline.has_value()) {
      const auto now = base::Time::NowFromSystemTime();
      // `=` here to avoid interval is set to 0 by any chance.
      if (now >= *deadline) {
        return false;
      }
      poll_interval = std::min(poll_interval, *deadline - now);
    }
    // This cast is safe since the value is guaranteed to be between 0 and
    // kDefaultPollInterval.InMilliseconds().
    int poll_interval_int = static_cast<int>(poll_interval.InMilliseconds());
    int ret = poll(pollfds, 2, poll_interval_int);
    if (ret == -1) {
      PLOG(ERROR) << "Failed to poll() outputs for " << logging_tag;
      break;
    }
    if (!HandlePollEvent(&pollfds[0], str_stdout)) {
      LOG(ERROR) << "Failed to process stdout for " << logging_tag;
    }
    if (!HandlePollEvent(&pollfds[1], str_stderr)) {
      LOG(ERROR) << "Failed to process stderr for " << logging_tag;
    }
    if (pollfds[0].fd == kInvalidFd && pollfds[1].fd == kInvalidFd) {
      break;
    }
  }

  return true;
}

}  // namespace

int MinijailedProcessRunner::RunSyncDestroyWithTimeout(
    const std::vector<std::string>& argv,
    brillo::Minijail* mj,
    minijail* jail,
    bool log_failures,
    std::optional<base::TimeDelta> timeout,
    std::string* output) {
  const base::Time started_at = base::Time::NowFromSystemTime();
  std::optional<base::Time> deadline = std::nullopt;
  if (timeout.has_value()) {
    deadline = started_at + *timeout;
  }

  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  const std::string logging_tag =
      base::StrCat({"'", base::JoinString(argv, " "), "'"});

  pid_t pid;
  int fd_stdout = -1;
  int fd_stderr = -1;
  bool ran = mj->RunPipesAndDestroy(jail, args, &pid, /*stdin=*/nullptr,
                                    &fd_stdout, &fd_stderr);
  if (!ran) {
    LOG(ERROR) << "Could not execute " << logging_tag;
    return -1;
  }

  base::ScopedFD scoped_fd_stdout(fd_stdout);
  base::ScopedFD scoped_fd_stderr(fd_stderr);
  std::string stderr_buf;
  if (!ReadPipesUntilClose(logging_tag, fd_stdout, fd_stderr, deadline, output,
                           log_failures ? &stderr_buf : nullptr)) {
    LOG(ERROR) << logging_tag << " has timed out";
    brillo::ProcessImpl process;
    process.Reset(pid);
    // Note that process.Kill() will also called waitpid() inside so we can just
    // return here.
    if (!process.Kill(SIGKILL, /*timeout=*/1)) {
      LOG(ERROR) << "Failed to kill " << logging_tag;
    }
    return -1;
  }

  base::TrimWhitespaceASCII(stderr_buf, base::TRIM_TRAILING, &stderr_buf);

  int status = 0;
  if (system_->WaitPid(pid, &status) == -1) {
    LOG(ERROR) << "Failed to waitpid() for " << logging_tag;
    return -1;
  }

  const base::TimeDelta duration = base::Time::NowFromSystemTime() - started_at;
  if (duration > base::Seconds(1)) {
    LOG(WARNING) << logging_tag << " took " << duration.InMilliseconds()
                 << "ms to finish.";
  }

  if (log_failures && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
    if (WIFEXITED(status)) {
      LOG(WARNING) << logging_tag << " exited with code "
                   << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      LOG(WARNING) << logging_tag << " exited with signal " << WTERMSIG(status);
    } else {
      LOG(WARNING) << logging_tag << " exited with unknown status " << status;
    }
    if (!stderr_buf.empty()) {
      LOG(WARNING) << "stderr: " << stderr_buf;
    }
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void EnterChildProcessJail() {
  brillo::Minijail* m = brillo::Minijail::GetInstance();
  struct minijail* jail = m->New();

  // Most of these return void, but DropRoot() can fail if the user/group
  // does not exist.
  CHECK(m->DropRoot(jail, kPatchpaneldUser, kPatchpaneldGroup))
      << "Could not drop root privileges";
  m->UseCapabilities(jail, kNetRawCapMask);
  m->Enter(jail);
  m->Destroy(jail);
}

MinijailedProcessRunner::MinijailedProcessRunner(brillo::Minijail* mj)
    : MinijailedProcessRunner(mj ? mj : brillo::Minijail::GetInstance(),
                              std::make_unique<System>()) {}

MinijailedProcessRunner::MinijailedProcessRunner(brillo::Minijail* mj,
                                                 std::unique_ptr<System> system)
    : mj_(mj), system_(std::move(system)) {}

int MinijailedProcessRunner::Run(const std::vector<std::string>& argv,
                                 bool log_failures) {
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser));
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);
  return RunSyncDestroy(argv, mj_, jail, log_failures, nullptr);
}

int MinijailedProcessRunner::ip(const std::string& obj,
                                const std::string& cmd,
                                const std::vector<std::string>& argv,
                                bool log_failures) {
  std::vector<std::string> args = {kIpPath, obj, cmd};
  args.insert(args.end(), argv.begin(), argv.end());
  return Run(args, log_failures);
}

int MinijailedProcessRunner::ip6(const std::string& obj,
                                 const std::string& cmd,
                                 const std::vector<std::string>& argv,
                                 bool log_failures) {
  std::vector<std::string> args = {kIpPath, "-6", obj, cmd};
  args.insert(args.end(), argv.begin(), argv.end());
  return Run(args, log_failures);
}

int MinijailedProcessRunner::iptables(Iptables::Table table,
                                      Iptables::Command command,
                                      base::StringPiece chain,
                                      const std::vector<std::string>& argv,
                                      bool log_failures,
                                      std::optional<base::TimeDelta> timeout,
                                      std::string* output) {
  return RunIptables(kIptablesPath, table, command, chain, argv, log_failures,
                     timeout, output);
}

int MinijailedProcessRunner::ip6tables(Iptables::Table table,
                                       Iptables::Command command,
                                       base::StringPiece chain,
                                       const std::vector<std::string>& argv,
                                       bool log_failures,
                                       std::optional<base::TimeDelta> timeout,
                                       std::string* output) {
  return RunIptables(kIp6tablesPath, table, command, chain, argv, log_failures,
                     timeout, output);
}

int MinijailedProcessRunner::RunIptables(std::string_view iptables_path,
                                         Iptables::Table table,
                                         Iptables::Command command,
                                         std::string_view chain,
                                         const std::vector<std::string>& argv,
                                         bool log_failures,
                                         std::optional<base::TimeDelta> timeout,
                                         std::string* output) {
  std::vector<std::string> args = {std::string(iptables_path), "-t",
                                   Iptables::TableName(table),
                                   Iptables::CommandName(command)};
  // TODO(b/278486416): Datapath::DumpIptables() needs support for passing an
  // empty chain. However, we cannot pass an empty argument to iptables
  // directly, so |chain| must be skipped in that case. Remove this temporary
  // work-around once chains are passed with an enum or a better data type.
  if (!chain.empty()) {
    args.push_back(std::string(chain));
  }
  args.insert(args.end(), argv.begin(), argv.end());

  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kPatchpaneldUser, kPatchpaneldGroup));
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);

  // Set up seccomp filter.
  mj_->UseSeccompFilter(jail, kIptablesSeccompFilterPath);

  return RunSyncDestroyWithTimeout(args, mj_, jail, log_failures, timeout,
                                   output);
}

int MinijailedProcessRunner::modprobe_all(
    const std::vector<std::string>& modules, bool log_failures) {
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser));
  mj_->UseCapabilities(jail, kModprobeCapMask);
  std::vector<std::string> args = {kModprobePath, "-a"};
  args.insert(args.end(), modules.begin(), modules.end());
  return RunSyncDestroy(args, mj_, jail, log_failures, nullptr);
}

int MinijailedProcessRunner::ip_netns_add(const std::string& netns_name,
                                          bool log_failures) {
  std::vector<std::string> args = {kIpPath, "netns", "add", netns_name};
  return RunIpNetns(args, log_failures);
}

int MinijailedProcessRunner::ip_netns_attach(const std::string& netns_name,
                                             pid_t netns_pid,
                                             bool log_failures) {
  std::vector<std::string> args = {kIpPath, "netns", "attach", netns_name,
                                   std::to_string(netns_pid)};
  return RunIpNetns(args, log_failures);
}

int MinijailedProcessRunner::ip_netns_delete(const std::string& netns_name,
                                             bool log_failures) {
  std::vector<std::string> args = {kIpPath, "netns", "delete", netns_name};
  return RunIpNetns(args, log_failures);
}

int MinijailedProcessRunner::RunIpNetns(const std::vector<std::string>& argv,
                                        bool log_failures) {
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kPatchpaneldUser, kPatchpaneldGroup));
  mj_->UseCapabilities(jail, kIpNetnsCapMask);
  return RunSyncDestroy(argv, mj_, jail, log_failures, nullptr);
}

int MinijailedProcessRunner::conntrack(std::string_view command,
                                       const std::vector<std::string>& argv,
                                       bool log_failures) {
  std::vector<std::string> args = {std::string(kConntrackPath),
                                   std::string(command)};
  args.insert(args.end(), argv.begin(), argv.end());

  // TODO(b/178980202): insert a seccomp filter right from the start for
  // conntrack.
  minijail* jail = mj_->New();
  CHECK(mj_->DropRoot(jail, kPatchpaneldUser, kPatchpaneldGroup));
  mj_->UseCapabilities(jail, kNetAdminCapMask);
  return RunSyncDestroy(args, mj_, jail, log_failures, nullptr);
}

}  // namespace patchpanel
