// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_
#define PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_

#include <sys/types.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/time/time.h>
#include <brillo/minijail/minijail.h>

#include "patchpanel/iptables.h"
#include "patchpanel/system.h"

namespace patchpanel {

// Runs the current process with minimal privileges. This function is expected
// to be used by child processes that need only CAP_NET_RAW and to run as the
// patchpaneld user.
void EnterChildProcessJail();

// Enforces the expected processes are run with the correct privileges.
class MinijailedProcessRunner {
 public:
  // Run iptables in batch with iptables-restore. See the comments for
  // AcquireIptablesBatchMode() below.
  class ScopedIptablesBatchMode {
   public:
    ~ScopedIptablesBatchMode();

   private:
    friend class MinijailedProcessRunner;  // to access ctor

    ScopedIptablesBatchMode(MinijailedProcessRunner* runner, bool* success);

    MinijailedProcessRunner* runner_;
    bool* success_;
  };

  static MinijailedProcessRunner* GetInstance();

  static std::unique_ptr<MinijailedProcessRunner> CreateForTesting(
      brillo::Minijail* mj, std::unique_ptr<System> system);

  MinijailedProcessRunner(const MinijailedProcessRunner&) = delete;
  MinijailedProcessRunner& operator=(const MinijailedProcessRunner&) = delete;

  virtual ~MinijailedProcessRunner() = default;

  // Runs ip. If |as_patchpanel_user|, runs as user 'patchpaneld' and under the
  // group 'patchpaneld', as well as inherits supplemntary groups (i.e. group
  // 'tun') of user 'patchpaneld'. If not, runs as 'nobody'.
  virtual int ip(const std::string& obj,
                 const std::string& cmd,
                 const std::vector<std::string>& args,
                 bool as_patchpanel_user = false,
                 bool log_failures = true);
  virtual int ip6(const std::string& obj,
                  const std::string& cmd,
                  const std::vector<std::string>& args,
                  bool as_patchpanel_user = false,
                  bool log_failures = true);

  // Acquires a "lock" to instruct this class to execute the following
  // iptables() and ip6tables() calls in batch. In detail:
  // - After this function is called, the semantics of the iptables() and
  //   ip6tables() call will be changed: the iptables (or ip6tables) won't be
  //   issued immediately after called, and the rule will be cached instead.
  //   |log_failures| in the function param won't have effect in this mode, and
  //   the returning success only indicates that the input argument passes the
  //   basic check instead of the rule is executed successfully.
  // - When the returned object of this function is destructed, all the cached
  //   iptables commands will be executed using iptables-restore. If |success|
  //   is not nullptr, the execution result will be written into it so that the
  //   caller can read it. |success| will be reset to false after this function
  //   is called.
  // - Due to the nature of iptables-restore, the cached rules can be partially
  //   submitted on failure (if the cached rules are for multiple tables). Thus
  //   all the rules in batch must be expected to succeed.
  std::unique_ptr<ScopedIptablesBatchMode> AcquireIptablesBatchMode(
      bool* success = nullptr);

  // Runs iptables. If |output| is not nullptr, it will be filled with the
  // result from stdout of iptables command.
  virtual int iptables(Iptables::Table table,
                       Iptables::Command command,
                       std::string_view chain,
                       const std::vector<std::string>& argv,
                       bool log_failures = true,
                       std::string* output = nullptr);

  virtual int ip6tables(Iptables::Table table,
                        Iptables::Command command,
                        std::string_view chain,
                        const std::vector<std::string>& argv,
                        bool log_failures = true,
                        std::string* output = nullptr);

  // Installs all |modules| via modprobe.
  virtual int modprobe_all(const std::vector<std::string>& modules,
                           bool log_failures = true);

  // Creates a new named network namespace with name |netns_name|.
  virtual int ip_netns_add(const std::string& netns_name,
                           bool log_failures = true);

  // Attaches a name to the network namespace of the given pid
  // TODO(hugobenichi) How can patchpanel create a |netns_name| file in
  // /run/netns without running ip as root ?
  virtual int ip_netns_attach(const std::string& netns_name,
                              pid_t netns_pid,
                              bool log_failures = true);

  virtual int ip_netns_delete(const std::string& netns_name,
                              bool log_failures = true);

  // Run conntrack command with given command option and |argv|.
  virtual int conntrack(std::string_view command,
                        const std::vector<std::string>& argv,
                        bool log_failures = true);

  virtual int iptables_restore(std::string_view script_file,
                               bool log_failures = true);
  virtual int ip6tables_restore(std::string_view script_file,
                                bool log_failures = true);

 protected:
  MinijailedProcessRunner();

  // Used by ip() and ip6().
  // Runs a process (argv[0]) with optional arguments (argv[1]...)
  // in a minijail as user |patchpaneld| and user the group |patchpaneld| with
  // CAP_NET_ADMIN and CAP_NET_RAW capabilities. Inherits supplementary groups
  // of |patchpaneld|.
  virtual int RunIp(const std::vector<std::string>& argv,
                    bool as_patchpanel_user,
                    bool log_failures = true);

  virtual int RunIptables(std::string_view iptables_path,
                          Iptables::Table table,
                          Iptables::Command command,
                          std::string_view chain,
                          const std::vector<std::string>& argv,
                          bool log_failures,
                          std::string* output);

  virtual int RunIpNetns(const std::vector<std::string>& argv,
                         bool log_failures);

 private:
  friend base::LazyInstanceTraitsBase<MinijailedProcessRunner>;

  int RunSyncDestroy(const std::vector<std::string>& argv,
                     brillo::Minijail* mj,
                     minijail* jail,
                     bool log_failures,
                     std::string* output);

  using TableToRules = std::map<Iptables::Table, std::vector<std::string>>;

  void AppendPendingIptablesRule(Iptables::Table table,
                                 Iptables::Command command,
                                 std::string_view chain,
                                 const std::vector<std::string>& argv,
                                 TableToRules* cached_rules);
  bool RunPendingIptablesInBatch();
  bool RunPendingIptablesInBatchImpl(std::string_view iptables_restore_path,
                                     const TableToRules& table_to_rules);

  brillo::Minijail* mj_;
  std::unique_ptr<System> system_;

  bool iptables_batch_mode_ = false;
  TableToRules pending_iptables_rules_;
  TableToRules pending_ip6tables_rules_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_
