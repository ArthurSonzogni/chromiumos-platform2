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
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd {

// The secagentd main daemon.
// On startup the device policy is fetched. Based on the security collection
// policies certain BPFs will be loaded and attached.
// These BPFs will produce security events that are collected by this daemon,
// which are packaged into protobuffs and sent to missived for delivery
// to an off-machine service.

class Daemon : public brillo::DBusDaemon {
  struct Inject {
    std::unique_ptr<PluginFactoryInterface> plugin_factory_;
    std::unique_ptr<policy::PolicyProvider> policy_provider_;
    scoped_refptr<MessageSender> message_sender_;
    scoped_refptr<ProcessCacheInterface> process_cache_;
  };

 public:
  Daemon() = delete;
  /* dependency injection for unit tests */
  explicit Daemon(struct Inject);
  Daemon(bool bypass_policy_for_testing, bool bypass_enq_ok_wait_for_testing);
  ~Daemon() override = default;

 protected:
  int OnInit() override;
  int OnEventLoopStarted() override;
  void OnShutdown(int*) override;
  // Creates and runs the agent plugin. The agent plugin will call back and run
  // the other plugins after agent start event is successfully sent.
  int CreateAndRunAgentPlugin();
  // Runs all of the plugin within the plugins_ vector.
  void RunPlugins();
  // Creates plugin of the given type.
  int CreatePlugin(Types::Plugin);
  // Return true if xdr_reporting_policy_ has changed.
  bool XdrReportingIsEnabled();
  // Polls the current policy for Xdr reporting every 10 minutes. Starts/stops
  // reporting depending on the policy.
  void PollXdrReportingIsEnabled();

 private:
  base::RepeatingTimer check_xdr_reporting_timer_;
  scoped_refptr<MessageSender> message_sender_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  std::unique_ptr<PluginFactoryInterface> plugin_factory_;
  std::vector<std::unique_ptr<PluginInterface>> plugins_;
  std::unique_ptr<PluginInterface> agent_plugin_;
  std::unique_ptr<policy::PolicyProvider> policy_provider_;
  bool bypass_policy_for_testing_ = false;
  bool bypass_enq_ok_wait_for_testing_ = false;
  bool xdr_reporting_policy_ = false;
};
};  // namespace secagentd

#endif  // SECAGENTD_DAEMON_H_
