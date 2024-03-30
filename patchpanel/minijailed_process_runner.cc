// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/minijailed_process_runner.h"

#include <linux/capability.h>
#include <linux/filter.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/containers/contains.h>
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
    "/usr/share/policy/iptables-seccomp.bpf.policy";

base::LazyInstance<MinijailedProcessRunner>::DestructorAtExit
    g_process_runner_ = LAZY_INSTANCE_INITIALIZER;

// Does some simple check for whether |token| can be fed to iptables. The main
// purpose is to avoid that one token can be interpreted as two, or multiple
// tokens can be interpreted as one.
bool IsValidTokenForIptables(std::string_view token) {
  const auto is_invalid_char = [](char c) {
    return std::isspace(c) || c == '\'' || c == '"';
  };

  return std::find_if(token.begin(), token.end(), is_invalid_char) ==
         token.end();
}

// The implementation logic is copied from
// src/platform/minijail/minijail0_cli.c:read_seccomp_filter().
bool LoadSeccompFilter(const base::FilePath& policy_bpf_file,
                       std::vector<struct sock_filter>* output_data,
                       struct sock_fprog* output_sock_fprog) {
  base::ScopedFILE f(fopen(policy_bpf_file.value().c_str(), "re"));
  if (!f) {
    PLOG(ERROR) << "Failed to open " << policy_bpf_file;
    return false;
  }
  off_t file_size = -1;
  if (fseeko(f.get(), 0, SEEK_END) == -1 ||
      (file_size = ftello(f.get())) == -1) {
    PLOG(ERROR) << "Failed to get size of " << policy_bpf_file;
    return false;
  }

  if (file_size % int{sizeof(struct sock_filter)} != 0) {
    LOG(ERROR) << "The policy file " << policy_bpf_file
               << " has an invalid size " << file_size;
    return false;
  }
  rewind(f.get());

  auto filter_size =
      static_cast<size_t>(file_size) / sizeof(struct sock_filter);
  output_data->resize(filter_size);
  if (fread(output_data->data(), sizeof(struct sock_filter), filter_size,
            f.get()) != filter_size) {
    PLOG(ERROR) << "Failed to read " << policy_bpf_file;
    return false;
  }

  output_sock_fprog->len = static_cast<uint16_t>(output_data->size());
  output_sock_fprog->filter = output_data->data();

  return true;
}

}  // namespace

int MinijailedProcessRunner::RunSyncDestroy(
    const std::vector<std::string>& argv,
    brillo::Minijail* mj,
    minijail* jail,
    bool log_failures,
    std::string* output) {
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

MinijailedProcessRunner::MinijailedProcessRunner()
    : mj_(brillo::Minijail::GetInstance()), system_(new System()) {}

MinijailedProcessRunner::MinijailedProcessRunner(brillo::Minijail* mj,
                                                 std::unique_ptr<System> system)
    : mj_(mj), system_(std::move(system)) {}

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
                                      std::string* output) {
  if (iptables_batch_mode_) {
    CHECK_EQ(output, nullptr);
    bool success = AppendPendingIptablesRule(table, command, chain, argv,
                                             &pending_iptables_rules_);
    return success ? 0 : -1;
  }

  return RunIptables(kIptablesPath, table, command, chain, argv, log_failures,
                     output);
}

int MinijailedProcessRunner::ip6tables(Iptables::Table table,
                                       Iptables::Command command,
                                       std::string_view chain,
                                       const std::vector<std::string>& argv,
                                       bool log_failures,
                                       std::string* output) {
  if (iptables_batch_mode_) {
    CHECK_EQ(output, nullptr);
    bool success = AppendPendingIptablesRule(table, command, chain, argv,
                                             &pending_ip6tables_rules_);
    return success ? 0 : -1;
  }

  return RunIptables(kIp6tablesPath, table, command, chain, argv, log_failures,
                     output);
}

int MinijailedProcessRunner::RunIptables(std::string_view iptables_path,
                                         Iptables::Table table,
                                         Iptables::Command command,
                                         std::string_view chain,
                                         const std::vector<std::string>& argv,
                                         bool log_failures,
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

  UseIptablesSeccompFilter(jail);

  return RunSyncDestroy(args, mj_, jail, log_failures, output);
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
  return RunIptablesRestore(kIptablesRestorePath, script_file, log_failures);
}

int MinijailedProcessRunner::ip6tables_restore(std::string_view script_file,
                                               bool log_failures) {
  return RunIptablesRestore(kIp6tablesRestorePath, script_file, log_failures);
}

int MinijailedProcessRunner::RunIptablesRestore(
    std::string_view iptables_restore_path,
    std::string_view script_file,
    bool log_failures) {
  std::vector<std::string> args = {std::string(iptables_restore_path),
                                   std::string(script_file), "-w"};

  minijail* jail = mj_->New();
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);
  UseIptablesSeccompFilter(jail);
  return RunSyncDestroy(args, mj_, jail, log_failures, nullptr);
}

using ScopedIptablesBatchMode =
    MinijailedProcessRunner::ScopedIptablesBatchMode;

ScopedIptablesBatchMode::ScopedIptablesBatchMode(
    MinijailedProcessRunner* runner)
    : runner_(runner) {}

ScopedIptablesBatchMode::~ScopedIptablesBatchMode() {
  if (runner_->iptables_batch_mode_) {
    runner_->RunPendingIptablesInBatch();
  }
}

std::unique_ptr<ScopedIptablesBatchMode>
MinijailedProcessRunner::AcquireIptablesBatchMode() {
  if (iptables_batch_mode_) {
    LOG(ERROR) << "Already in iptables batch mode";
    return nullptr;
  }
  iptables_batch_mode_ = true;
  return base::WrapUnique(new ScopedIptablesBatchMode(this));
}

bool MinijailedProcessRunner::CommitIptablesRules(
    std::unique_ptr<ScopedIptablesBatchMode> batch_mode) {
  CHECK(batch_mode);
  return RunPendingIptablesInBatch();
}

bool MinijailedProcessRunner::AppendPendingIptablesRule(
    Iptables::Table table,
    Iptables::Command command,
    std::string_view chain,
    const std::vector<std::string>& argv,
    TableToRules* pending_rules) {
  // A few args for calling iptables are not generated by patchpanel itself
  // (e.g., some interface names). Let's do some basic check here to avoid any
  // possibilities of injection when calling iptables (e.g, the input is "\n-I
  // -j ACCEPT").
  if (!IsValidTokenForIptables(chain)) {
    LOG(ERROR) << "Invalid chain name " << chain;
    return false;
  }
  for (const auto& arg : argv) {
    if (!IsValidTokenForIptables(arg)) {
      LOG(ERROR) << "Invalid input for iptables " << arg;
      return false;
    }
  }

  std::vector<std::string> args;
  using Command = Iptables::Command;
  switch (command) {
    case Command::kA:
    case Command::kD:
    case Command::kF:
    case Command::kI:
    case Command::kX:
      args = {Iptables::CommandName(command), std::string(chain)};
      args.insert(args.end(), argv.begin(), argv.end());
      break;
    case Command::kN:
      // Convert `-N chain` to `:chain - [0:0]`, which will flush the rules and
      // reset counters if the chain exist, or create a new chain otherwise.
      args = {base::StrCat({":", chain, " - [0:0]"})};
      break;
    case Command::kL:
    case Command::kS:
    case Command::kC:
      // These commands are meaningful in iptables-restore, but do not make
      // sense here.
      CHECK(false);
  }

  // TODO(jiejiang): Remove "-w" when calling iptables()/ip6tables().
  if (args.back() == "-w") {
    args.pop_back();
  }
  CHECK(!base::Contains(args, "-w"));

  (*pending_rules)[table].push_back(base::JoinString(args, " "));

  return true;
}

bool MinijailedProcessRunner::RunPendingIptablesInBatch() {
  CHECK(iptables_batch_mode_);
  iptables_batch_mode_ = false;
  bool success = true;
  success &= RunPendingIptablesInBatchImpl(kIptablesRestorePath,
                                           pending_iptables_rules_);
  pending_iptables_rules_.clear();
  success &= RunPendingIptablesInBatchImpl(kIp6tablesRestorePath,
                                           pending_ip6tables_rules_);
  pending_ip6tables_rules_.clear();
  return success;
}

bool MinijailedProcessRunner::RunPendingIptablesInBatchImpl(
    std::string_view iptables_restore_path,
    const TableToRules& table_to_rules) {
  if (table_to_rules.empty()) {
    // We may have rules only for IPv4 or IPv6, so this is expected.
    return true;
  }

  std::vector<std::string> lines;
  for (const auto& [table, rules] : table_to_rules) {
    lines.push_back(base::StrCat({"*", Iptables::TableName(table)}));
    lines.insert(lines.end(), rules.begin(), rules.end());
    // Need a "\n" after "COMMIT". Add it here since JoinString() won't do
    // it for the last line.
    lines.push_back("COMMIT\n");
  }

  std::string input = base::JoinString(lines, "\n");

  // TODO(b/328151873): Write to the stdin pipe would be easier in logic but
  // complicated in implementation now. Refactor this after we have a better
  // Process abstraction.
  base::ScopedFD script_fd(memfd_create("iptables-restore", /*flags=*/0));
  if (!script_fd.is_valid()) {
    PLOG(ERROR) << "Failed to create input file to iptables-restore";
    return false;
  }
  if (!base::WriteFileDescriptor(script_fd.get(), input)) {
    PLOG(ERROR) << "Failed to generate input file to iptables-restore";
    return false;
  }
  auto script_path = base::StringPrintf("/proc/self/fd/%d", script_fd.get());

  minijail* jail = mj_->New();
  mj_->UseCapabilities(jail, kNetRawAdminCapMask | kBPFCapMask);
  UseIptablesSeccompFilter(jail);

  int ret = RunSyncDestroy(
      {std::string(iptables_restore_path), "-n", script_path, "-w"}, mj_, jail,
      /*log_failures=*/true, nullptr);

  // TODO(b/328151873): Parse stderr so we can also log which line contains an
  // error.
  if (ret != 0) {
    LOG(ERROR) << iptables_restore_path << " exited with " << ret
               << ", input: " << input;
  }

  return ret == 0;
}

void MinijailedProcessRunner::UseIptablesSeccompFilter(minijail* jail) {
  // Read the binary seccomp filters for iptables. Crash the process on failure
  // since 1) this is not expected, 2) may indicate a security issue, 3) follow
  // the API design of libminijail (the following calls to libminijail will also
  // incur a crash on failure).
  if (iptables_seccomp_filter_data_.empty()) {
    if (!LoadSeccompFilter(base::FilePath(kIptablesSeccompFilterPath),
                           &iptables_seccomp_filter_data_,
                           &iptables_seccomp_filter_)) {
      LOG(FATAL) << "Failed to load seccomp filter for iptables";
    }
  }

  minijail_no_new_privs(jail);
  minijail_use_seccomp_filter(jail);
  minijail_set_seccomp_filters(jail, &iptables_seccomp_filter_);
}

}  // namespace patchpanel
