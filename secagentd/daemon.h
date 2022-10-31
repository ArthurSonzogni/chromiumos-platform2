// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_DAEMON_H_
#define SECAGENTD_DAEMON_H_

#include <memory>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/timer/timer.h"
#include "brillo/daemons/dbus_daemon.h"
#include "policy/libpolicy.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/process_cache.h"

namespace secagentd {

// The secagentd main daemon.
// On startup the device policy is fetched. Based on the security collection
// policies certain BPFs will be loaded and attached.
// These BPFs will produce security events that are collected by this daemon,
// which are packaged into protobuffs and sent to missived for delivery
// to an off-machine service.

class Daemon : public brillo::DBusDaemon {
  struct Inject {
    std::unique_ptr<PluginFactoryInterface> bpf_plugin_factory_;
    std::unique_ptr<policy::PolicyProvider> policy_provider_;
    scoped_refptr<MessageSender> message_sender_;
    scoped_refptr<ProcessCacheInterface> process_cache_;
  };

 public:
  Daemon() = default;
  /* dependency injection for unit tests */
  explicit Daemon(struct Inject);
  ~Daemon() override = default;

 protected:
  int OnInit() override;
  int OnEventLoopStarted() override;
  void OnShutdown(int*) override;
  int RunPlugins();
  int CreateAndRunBpfPlugins();
  int CreateAndRunAgentPlugins();
  // Return true if xdr_reporting_policy_ has changed.
  bool XdrReportingIsEnabled();
  void PollXdrReportingIsEnabled();

 private:
  base::RepeatingTimer check_xdr_reporting_timer_;
  base::RepeatingTimer heart_beat_;
  base::RepeatingTimer send_report_;
  scoped_refptr<MessageSender> message_sender_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  std::unique_ptr<PluginFactoryInterface> bpf_plugin_factory_;
  std::vector<std::unique_ptr<PluginInterface>> bpf_plugins_;
  std::unique_ptr<policy::PolicyProvider> policy_provider_;
  bool xdr_reporting_policy_;
};
};  // namespace secagentd

#endif  // SECAGENTD_DAEMON_H_
