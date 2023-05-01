// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_SECAGENT_H_
#define SECAGENTD_SECAGENT_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "attestation/proto_bindings/interface.pb.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/memory/scoped_refptr.h"
#include "dbus/mock_bus.h"
#include "featured/feature_library.h"
#include "secagentd/device_user.h"
#include "secagentd/message_sender.h"
#include "secagentd/metrics_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/process_cache.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "session_manager/dbus-proxies.h"
#include "tpm_manager/dbus-proxies.h"
#include "tpm_manager/proto_bindings/tpm_manager.pb.h"
#include "tpm_manager-client/tpm_manager/dbus-proxies.h"

namespace secagentd {

namespace testing {
class SecAgentTestFixture;
}

class SecAgent {
 public:
  SecAgent() = delete;
  SecAgent(base::OnceCallback<void(int)>,
           scoped_refptr<MessageSenderInterface>,
           scoped_refptr<ProcessCacheInterface>,
           scoped_refptr<DeviceUserInterface>,
           std::unique_ptr<PluginFactoryInterface>,
           std::unique_ptr<org::chromium::AttestationProxyInterface>,
           std::unique_ptr<org::chromium::TpmManagerProxyInterface>,
           std::unique_ptr<feature::PlatformFeaturesInterface>,
           bool bypass_policy_for_testing,
           bool bypass_enq_ok_wait_for_testing,
           uint32_t heartbeat_period_s,
           uint32_t plugin_batch_interval_s);
  ~SecAgent() = default;

  // Start polling for policy and feature flags.
  void Activate();
  // Checks the status of the XDR feature flag and policy flag. Starts/stops
  // reporting as necessary.
  void CheckPolicyAndFeature();

 protected:
  // Runs all of the plugin within the plugins_ vector.
  void RunPlugins();
  // Creates plugin of the given type.
  int CreatePlugin(Types::Plugin);
  // Starts the plugin loading process. First creates the agent plugin and
  // waits for a successfully sent heartbeat before creating and running
  // the remaining plugins.
  void StartXDRReporting();

 private:
  friend class testing::SecAgentTestFixture;

  scoped_refptr<MessageSenderInterface> message_sender_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker_;
  scoped_refptr<DeviceUserInterface> device_user_;
  std::unique_ptr<PluginFactoryInterface> plugin_factory_;
  std::vector<std::unique_ptr<PluginInterface>> plugins_;
  std::unique_ptr<PluginInterface> agent_plugin_;
  std::unique_ptr<org::chromium::AttestationProxyInterface> attestation_proxy_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_proxy_;
  std::unique_ptr<feature::PlatformFeaturesInterface> platform_features_;
  bool bypass_policy_for_testing_ = false;
  bool bypass_enq_ok_wait_for_testing_ = false;
  bool reporting_events_ = false;
  uint32_t heartbeat_period_s_;
  uint32_t plugin_batch_interval_s_;
  base::OnceCallback<void(int)> quit_daemon_cb_;
  base::WeakPtrFactory<SecAgent> weak_ptr_factory_;
};
};      // namespace secagentd
#endif  // SECAGENTD_SECAGENT_H_
