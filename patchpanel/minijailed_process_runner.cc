// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/minijailed_process_runner.h"

#include <linux/capability.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
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

// - 39 for CAP_BPF. This does not exist on all kernels so we need to define it
//   here.
// - CAP_TO_MASK() only works for a CAP whose index is less than 32.
//
// TODO(b/311100871): Switch to use CAP_BPF after all kernels are 5.8+.
constexpr uint64_t kBPFCapMask = 1ull << 39;

// `ip netns` needs CAP_SYS_ADMIN for mount(), and CAP_SYS_PTRACE for accessing
// `/proc/${pid}/ns/net` of other processes.
constexpr uint64_t kIpNetnsCapMask =
    CAP_TO_MASK(CAP_SYS_PTRACE) | CAP_TO_MASK(CAP_SYS_ADMIN);

// These match what is used in iptables.cc in firewalld.
constexpr char kIpPath[] = "/bin/ip";
constexpr char kIptablesPath[] = "/sbin/iptables";
constexpr char kIp6tablesPath[] = "/sbin/ip6tables";
constexpr std::string_view kIptablesRestorePath = "/sbin/iptables-restore";
constexpr std::string_view kIp6tablesRestorePath = "/sbin/ip6tables-restore";

constexpr char kModprobePath[] = "/sbin/modprobe";
constexpr char kConntrackPath[] = "/usr/sbin/conntrack";

constexpr char kIptablesSeccompFilterPath[] =
    "/usr/share/policy/iptables-seccomp.policy";

base::LazyInstance<MinijailedProcessRunner>::DestructorAtExit
    g_process_runner_ = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// TODO(jiejiang): `timeout` is not used now. We should consider refactor this
// function to a Process class (e.g., implement brillo::Process interface) to
// reduce the code complexity of this function.
int MinijailedProcessRunner::RunSyncDestroyWithTimeout(
    const std::vector<std::string>& argv,
    brillo::Minijail* mj,
    minijail* jail,
    bool log_failures,
    std::optional<base::TimeDelta> timeout,
    std::string* output) {
  CHECK(!timeout.has_value());

  const base::TimeTicks started_at = base::TimeTicks::Now();

  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  const std::string logging_tag =
      base::StrCat({"'", base::JoinString(argv, " "), "'"});

  // Helper function to redirect a child fd to an anonymous file in memory.
  // `name` will only be used for logging and debugging purposes, and can be
  // same for different fds. Note that the created anonymous file will be
  // removed automatically after there is no reference on it.
  auto redirect_child_fd =
      [&jail, &logging_tag](int child_fd, const char* name) -> base::ScopedFD {
    base::ScopedFD memfd(memfd_create(name, /*flags=*/0));
    if (!memfd.is_valid()) {
      PLOG(ERROR) << "Failed to create memfd of " << name << " for "
                  << logging_tag;
      return {};
    }
    if (minijail_preserve_fd(jail, memfd.get(), child_fd) != 0) {
      PLOG(ERROR) << "Failed to preserve fd of " << name << " for "
                  << logging_tag;
      return {};
    }
    return memfd;
  };

  // Helper function to read from the file created by `redirect_child_fd`.
  auto read_memfd_to_string = [&logging_tag](int fd, std::string* out) {
    auto path = base::StringPrintf("/proc/self/fd/%d", fd);
    if (!base::ReadFileToString(base::FilePath(path), out)) {
      PLOG(ERROR) << "Failed to read " << path << " for " << logging_tag;
    }
  };

  base::ScopedFD stdout_fd, stderr_fd;
  if (output) {
    stdout_fd = redirect_child_fd(STDOUT_FILENO, "stdout");
    if (!stdout_fd.is_valid()) {
      return -1;
    }
  }
  if (log_failures) {
    stderr_fd = redirect_child_fd(STDERR_FILENO, "stderr");
    if (!stderr_fd.is_valid()) {
      return -1;
    }
  }

  pid_t pid;
  if (!mj->RunAndDestroy(jail, args, &pid)) {
    LOG(ERROR) << "Could not execute " << logging_tag;
    return -1;
  }

  int status = 0;
  if (system_->WaitPid(pid, &status) == -1) {
    LOG(ERROR) << "Failed to waitpid() for " << logging_tag;
    return -1;
  }

  const base::TimeDelta duration = base::TimeTicks::Now() - started_at;
  if (duration > base::Seconds(1)) {
    LOG(WARNING) << logging_tag << " took " << duration.InMilliseconds()
                 << "ms to finish.";
  }

  if (stdout_fd.is_valid()) {
    read_memfd_to_string(stdout_fd.get(), output);
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
    std::string stderr_buf;
    read_memfd_to_string(stderr_fd.get(), &stderr_buf);
    base::TrimWhitespaceASCII(stderr_buf, base::TRIM_TRAILING, &stderr_buf);
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

MinijailedProcessRunner* MinijailedProcessRunner::GetInstance() {
  return g_process_runner_.Pointer();
}

std::unique_ptr<MinijailedProcessRunner>
MinijailedProcessRunner::CreateForTesting(brillo::Minijail* mj,
                                          std::unique_ptr<System> system) {
  auto ret = base::WrapUnique(new MinijailedProcessRunner);
  ret->mj_ = mj;
  ret->system_ = std::move(system);
  return ret;
}

MinijailedProcessRunner::MinijailedProcessRunner()
    : mj_(brillo::Minijail::GetInstance()), system_(new System()) {}

int MinijailedProcessRunner::RunIp(const std::vector<std::string>& argv,
                                   bool as_patchpanel_user,
                                   bool log_failures) {
  minijail* jail = mj_->New();
  if (as_patchpanel_user) {
    CHECK(mj_->DropRoot(jail, kPatchpaneldUser, kPatchpaneldGroup));
    minijail_inherit_usergroups(jail);
  } else {
    CHECK(mj_->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser));
  }
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);
  return RunSyncDestroy(argv, mj_, jail, log_failures, nullptr);
}

int MinijailedProcessRunner::ip(const std::string& obj,
                                const std::string& cmd,
                                const std::vector<std::string>& argv,
                                bool as_patchpanel_user,
                                bool log_failures) {
  std::vector<std::string> args = {kIpPath, obj, cmd};
  args.insert(args.end(), argv.begin(), argv.end());
  return RunIp(args, as_patchpanel_user, log_failures);
}

int MinijailedProcessRunner::ip6(const std::string& obj,
                                 const std::string& cmd,
                                 const std::vector<std::string>& argv,
                                 bool as_patchpanel_user,
                                 bool log_failures) {
  std::vector<std::string> args = {kIpPath, "-6", obj, cmd};
  args.insert(args.end(), argv.begin(), argv.end());
  return RunIp(args, as_patchpanel_user, log_failures);
}

int MinijailedProcessRunner::iptables(Iptables::Table table,
                                      Iptables::Command command,
                                      std::string_view chain,
                                      const std::vector<std::string>& argv,
                                      bool log_failures,
                                      std::optional<base::TimeDelta> timeout,
                                      std::string* output) {
  return RunIptables(kIptablesPath, table, command, chain, argv, log_failures,
                     timeout, output);
}

int MinijailedProcessRunner::ip6tables(Iptables::Table table,
                                       Iptables::Command command,
                                       std::string_view chain,
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

  // TODO(b/311100871): Only add CAP_BPF for iptables commands required that but
  // not all.
  mj_->UseCapabilities(jail, kNetRawAdminCapMask | kBPFCapMask);

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

int MinijailedProcessRunner::iptables_restore(std::string_view script_file,
                                              bool log_failures) {
  std::vector<std::string> args = {std::string(kIptablesRestorePath),
                                   std::string(script_file)};

  minijail* jail = mj_->New();
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);
  mj_->UseSeccompFilter(jail, kIptablesSeccompFilterPath);
  return RunSyncDestroy(args, mj_, jail, log_failures, nullptr);
}

int MinijailedProcessRunner::ip6tables_restore(std::string_view script_file,
                                               bool log_failures) {
  std::vector<std::string> args = {std::string(kIp6tablesRestorePath),
                                   std::string(script_file)};

  minijail* jail = mj_->New();
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);
  mj_->UseSeccompFilter(jail, kIptablesSeccompFilterPath);
  return RunSyncDestroy(args, mj_, jail, log_failures, nullptr);
}

}  // namespace patchpanel
