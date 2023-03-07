// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_DAEMON_H_
#define SECAGENTD_DAEMON_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "brillo/daemons/dbus_daemon.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"
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
    scoped_refptr<MessageSender> message_sender_;
    scoped_refptr<ProcessCacheInterface> process_cache_;
    scoped_refptr<PoliciesFeaturesBroker> policies_features_broker_;
  };

 public:
  static constexpr uint32_t kDefaultHeartbeatPeriodS = 300;
  static constexpr uint32_t kDefaultPluginBatchIntervalS = 2 * 60;

  Daemon() = delete;
  /* dependency injection for unit tests */
  explicit Daemon(struct Inject);
  Daemon(bool bypass_policy_for_testing,
         bool bypass_enq_ok_wait_for_testing,
         uint32_t heartbeat_period_s,
         uint32_t plugin_batch_interval_s);
  ~Daemon() override = default;

 protected:
  int OnInit() override;
  int OnEventLoopStarted() override;
  void OnShutdown(int*) override;
  // Runs all of the plugin within the plugins_ vector.
  void RunPlugins();
  // Creates plugin of the given type.
  int CreatePlugin(Types::Plugin);
  // Checks the status of the XDR feature flag and policy flag. Starts/stops
  // reporting as necessary.
  void CheckPolicyAndFeature();
  // Starts the plugin loading process. First creates the agent plugin and
  // waits for a successfully sent heartbeat before creating and running
  // the remaining plugins.
  void StartXDRReporting();

 private:
  scoped_refptr<MessageSender> message_sender_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<PoliciesFeaturesBroker> policies_features_broker_;
  std::unique_ptr<PluginFactoryInterface> plugin_factory_;
  std::vector<std::unique_ptr<PluginInterface>> plugins_;
  std::unique_ptr<PluginInterface> agent_plugin_;
  bool bypass_policy_for_testing_ = false;
  bool bypass_enq_ok_wait_for_testing_ = false;
  bool reporting_events_ = false;
  uint32_t heartbeat_period_s_ = kDefaultHeartbeatPeriodS;
  uint32_t plugin_batch_interval_s_ = kDefaultPluginBatchIntervalS;
  base::WeakPtrFactory<Daemon> weak_ptr_factory_;
};
};  // namespace secagentd

#endif  // SECAGENTD_DAEMON_H_
