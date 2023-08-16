// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/minijailed_process_runner.h"

#include <linux/capability.h>
#include <unistd.h>

#include <utility>

#include <base/check.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>

namespace patchpanel {

namespace {

constexpr char kUnprivilegedUser[] = "nobody";
constexpr uint64_t kModprobeCapMask = CAP_TO_MASK(CAP_SYS_MODULE);
constexpr uint64_t kNetRawCapMask = CAP_TO_MASK(CAP_NET_RAW);
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

constexpr char kIptablesSeccompFilterPath[] =
    "/usr/share/policy/iptables-seccomp.policy";

// An empty string will be returned if read fails.
std::string ReadBlockingFDToStringAndClose(base::ScopedFD fd) {
  if (!fd.is_valid()) {
    LOG(ERROR) << "Invalid fd";
    return "";
  }

  static constexpr int kBufSize = 2048;
  char buf[kBufSize] = {0};
  std::string output;
  while (true) {
    ssize_t cnt = HANDLE_EINTR(read(fd.get(), buf, kBufSize));
    if (cnt == -1) {
      PLOG(ERROR) << __func__ << " failed";
      return "";
    }

    if (cnt == 0) {
      return output;
    }

    output.append({buf, static_cast<size_t>(cnt)});
  }
}

}  // namespace

int MinijailedProcessRunner::RunSyncDestroy(
    const std::vector<std::string>& argv,
    brillo::Minijail* mj,
    minijail* jail,
    bool log_failures,
    std::string* output) {
  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  pid_t pid;
  int fd_stdout = -1;
  int* stdout_p = output ? &fd_stdout : nullptr;
  int fd_stderr = -1;
  int* stderr_p = log_failures ? &fd_stderr : nullptr;
  bool ran = mj->RunPipesAndDestroy(jail, args, &pid, /*stdin=*/nullptr,
                                    stdout_p, stderr_p);
  if (output) {
    *output = ReadBlockingFDToStringAndClose(base::ScopedFD(fd_stdout));
  }
  std::string stderr_buf;
  if (log_failures) {
    stderr_buf = ReadBlockingFDToStringAndClose(base::ScopedFD(fd_stderr));
    base::TrimWhitespaceASCII(stderr_buf, base::TRIM_TRAILING, &stderr_buf);
  }

  int status = 0;
  if (ran) {
    ran = system_->WaitPid(pid, &status) == pid;
  }

  if (!ran) {
    LOG(ERROR) << "Could not execute '" << base::JoinString(argv, " ") << "'";
  } else if (log_failures && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
    if (WIFEXITED(status)) {
      LOG(WARNING) << "Subprocess '" << base::JoinString(argv, " ")
                   << "' exited with code " << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      LOG(WARNING) << "Subprocess '" << base::JoinString(argv, " ")
                   << "' exited with signal " << WTERMSIG(status);
    } else {
      LOG(WARNING) << "Subprocess '" << base::JoinString(argv, " ")
                   << "' exited with unknown status " << status;
    }
    if (!stderr_buf.empty()) {
      LOG(WARNING) << "stderr: " << stderr_buf;
    }
  }
  return ran && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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
                                      std::string* output) {
  return RunIptables(kIptablesPath, table, command, chain, argv, log_failures,
                     output);
}

int MinijailedProcessRunner::ip6tables(Iptables::Table table,
                                       Iptables::Command command,
                                       base::StringPiece chain,
                                       const std::vector<std::string>& argv,
                                       bool log_failures,
                                       std::string* output) {
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
  CHECK(mj_->DropRoot(jail, kPatchpaneldUser, kPatchpaneldGroup));
  mj_->UseCapabilities(jail, kNetRawAdminCapMask);

  // Set up seccomp filter.
  mj_->UseSeccompFilter(jail, kIptablesSeccompFilterPath);

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

}  // namespace patchpanel
