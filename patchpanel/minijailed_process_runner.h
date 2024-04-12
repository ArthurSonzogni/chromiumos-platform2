// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_
#define PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_

#include <linux/filter.h>
#include <sys/types.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/containers/span.h>
#include <base/time/time.h>
#include <brillo/minijail/minijail.h>

#include "patchpanel/iptables.h"
#include "patchpanel/system.h"

namespace patchpanel {

// Runs the current process with minimal privileges. This function is expected
// to be used by child processes that need only CAP_NET_RAW and to run as the
// patchpaneld user.
void EnterChildProcessJail();

// Runs the current process with minimal privileges. This function is expected
// to be used by child processes that need only CAP_NET_ADMIN and to run as the
// patchpaneld user.
void EnterChildProcessJailWithNetAdmin();

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

    explicit ScopedIptablesBatchMode(MinijailedProcessRunner* runner);

    MinijailedProcessRunner* runner_;
  };

  static MinijailedProcessRunner* GetInstance();

  MinijailedProcessRunner(const MinijailedProcessRunner&) = delete;
  MinijailedProcessRunner& operator=(const MinijailedProcessRunner&) = delete;

  virtual ~MinijailedProcessRunner() = default;

  // Runs ip. If |as_patchpanel_user|, runs as user 'patchpaneld' and under the
  // group 'patchpaneld', as well as inherits supplemntary groups (i.e. group
  // 'tun') of user 'patchpaneld'. If not, runs as 'nobody'.
  virtual int ip(std::string_view obj,
                 std::string_view cmd,
                 base::span<const std::string> argv,
                 bool as_patchpanel_user = false,
                 bool log_failures = true);
  virtual int ip(std::string_view obj,
                 std::string_view cmd,
                 base::span<std::string_view> argv,
                 bool as_patchpanel_user = false,
                 bool log_failures = true);
  virtual int ip(std::string_view obj,
                 std::string_view cmd,
                 std::initializer_list<std::string_view> argv,
                 bool as_patchpanel_user = false,
                 bool log_failures = true);

  virtual int ip6(std::string_view obj,
                  std::string_view cmd,
                  base::span<const std::string> argv,
                  bool as_patchpanel_user = false,
                  bool log_failures = true);
  virtual int ip6(std::string_view obj,
                  std::string_view cmd,
                  base::span<std::string_view> argv,
                  bool as_patchpanel_user = false,
                  bool log_failures = true);
  virtual int ip6(std::string_view obj,
                  std::string_view cmd,
                  std::initializer_list<std::string_view> argv,
                  bool as_patchpanel_user = false,
                  bool log_failures = true);

  // Acquires a "lock" to instruct this class to execute the following
  // iptables() and ip6tables() calls in batch. In detail:
  // - After this function is called, the semantics of the iptables() and
  //   ip6tables() call will be changed: the iptables (or ip6tables) won't be
  //   issued immediately after called, and the rule will be cached instead.
  //   |log_failures| in the function param won't have effect in this mode, and
  //   the returning success only indicates that the input argument passes the
  //   basic check instead of the rule is executed successfully (return 0 on
  //   success or -1 otherwise).
  // - When either 1) the returned object of this function is destructed or 2)
  //   CommitIptablesRules() is called, all the cached iptables commands will be
  //   executed using iptables-restore. Their difference is that the caller can
  //   get the execution result with CommitIptablesRules().
  // - Due to the nature of iptables-restore, the cached rules can be partially
  //   submitted on failure (if the cached rules are for multiple tables). Thus
  //   all the rules in batch must be expected to succeed.
  virtual std::unique_ptr<ScopedIptablesBatchMode> AcquireIptablesBatchMode();

  // Executes the pending rules started from |batch_mode| is acquired. This
  // function has same effect with dropping |batch_mode| directly, except that
  // this function will return whether the execution succeeded or not.
  bool CommitIptablesRules(std::unique_ptr<ScopedIptablesBatchMode> batch_mode);

  // Runs iptables. If |output| is not nullptr, it will be filled with the
  // result from stdout of iptables command.
  virtual int iptables(Iptables::Table table,
                       Iptables::Command command,
                       std::string_view chain,
                       base::span<const std::string> argv,
                       bool log_failures = true,
                       std::string* output = nullptr);
  virtual int iptables(Iptables::Table table,
                       Iptables::Command command,
                       std::string_view chain,
                       base::span<std::string_view> argv,
                       bool log_failures = true,
                       std::string* output = nullptr);
  virtual int iptables(Iptables::Table table,
                       Iptables::Command command,
                       std::string_view chain,
                       std::initializer_list<std::string_view> argv,
                       bool log_failures = true,
                       std::string* output = nullptr);

  virtual int ip6tables(Iptables::Table table,
                        Iptables::Command command,
                        std::string_view chain,
                        base::span<const std::string> argv,
                        bool log_failures = true,
                        std::string* output = nullptr);
  virtual int ip6tables(Iptables::Table table,
                        Iptables::Command command,
                        std::string_view chain,
                        base::span<std::string_view> argv,
                        bool log_failures = true,
                        std::string* output = nullptr);
  virtual int ip6tables(Iptables::Table table,
                        Iptables::Command command,
                        std::string_view chain,
                        std::initializer_list<std::string_view> argv,
                        bool log_failures = true,
                        std::string* output = nullptr);

  // Installs all |modules| via modprobe.
  virtual int modprobe_all(base::span<const std::string> modules,
                           bool log_failures = true);

  // Creates a new named network namespace with name |netns_name|.
  virtual int ip_netns_add(std::string_view netns_name,
                           bool log_failures = true);

  // Attaches a name to the network namespace of the given pid
  // TODO(hugobenichi) How can patchpanel create a |netns_name| file in
  // /run/netns without running ip as root ?
  virtual int ip_netns_attach(std::string_view netns_name,
                              pid_t netns_pid,
                              bool log_failures = true);

  virtual int ip_netns_delete(std::string_view netns_name,
                              bool log_failures = true);

  // Run conntrack command with given command option and |argv|.
  virtual int conntrack(std::string_view command,
                        base::span<const std::string> argv,
                        bool log_failures = true);

  virtual int iptables_restore(std::string_view script_file,
                               bool log_failures = true);
  virtual int ip6tables_restore(std::string_view script_file,
                                bool log_failures = true);

 protected:
  // Constructor for the singleton.
  MinijailedProcessRunner();
  // Constructor used in the unit tests.
  MinijailedProcessRunner(brillo::Minijail* mj, std::unique_ptr<System> system);

  // Used by ip() and ip6().
  // Runs a process (argv[0]) with optional arguments (argv[1]...)
  // in a minijail as user |patchpaneld| and user the group |patchpaneld| with
  // CAP_NET_ADMIN and CAP_NET_RAW capabilities. Inherits supplementary groups
  // of |patchpaneld|.
  virtual int RunIp(base::span<std::string_view> argv,
                    bool as_patchpanel_user,
                    bool log_failures = true);

  virtual int RunIptables(std::string_view iptables_path,
                          Iptables::Table table,
                          Iptables::Command command,
                          std::string_view chain,
                          base::span<std::string_view> argv,
                          bool log_failures,
                          std::string* output);

  virtual int RunIptablesRestore(std::string_view iptables_restore_path,
                                 std::string_view script_file,
                                 bool log_failures);

  virtual int RunIpNetns(base::span<std::string_view> argv, bool log_failures);

  virtual bool RunPendingIptablesInBatch();

  // Converts string_view elements in |argv| to cstrings and returns a vector
  // of these cstrings. |buffer| is a view for underneath buffer used for
  // storing data of string_view. User is responsible to allocate and manage
  // the life cycle of underlying buffer.
  static std::vector<char*> StringViewToCstrings(
      base::span<std::string_view> argv, base::span<char> buffer);

 private:
  friend base::LazyInstanceTraitsBase<MinijailedProcessRunner>;

  int RunSyncDestroy(base::span<std::string_view> argv,
                     brillo::Minijail* mj,
                     minijail* jail,
                     bool log_failures,
                     std::string* output);

  using TableToRules = std::map<Iptables::Table, std::vector<std::string>>;

  bool AppendPendingIptablesRule(Iptables::Table table,
                                 Iptables::Command command,
                                 std::string_view chain,
                                 base::span<std::string_view> argv,
                                 TableToRules* cached_rules);
  bool RunPendingIptablesInBatchImpl(std::string_view iptables_restore_path,
                                     const TableToRules& table_to_rules);

  // Configures |jail| to apply the seccomp filter on iptables.
  virtual void UseIptablesSeccompFilter(minijail* jail);

  brillo::Minijail* mj_;
  std::unique_ptr<System> system_;

  bool iptables_batch_mode_ = false;
  TableToRules pending_iptables_rules_;
  TableToRules pending_ip6tables_rules_;

  // Only set and used in UseIptablesSeccompFilter(). See the implementation
  // there for details.
  std::vector<struct sock_filter> iptables_seccomp_filter_data_;
  struct sock_fprog iptables_seccomp_filter_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MINIJAILED_PROCESS_RUNNER_H_
