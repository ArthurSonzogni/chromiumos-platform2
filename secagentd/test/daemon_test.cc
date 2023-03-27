// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <secagentd/daemon.h>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>

#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"
#include "gmock/gmock.h"  // IWYU pragma: keep
#include "gtest/gtest.h"
#include "metrics/metrics_library.h"
#include "secagentd/common.h"
#include "secagentd/plugins.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_plugin_factory.h"
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"

namespace secagentd::testing {

namespace pb = cros_xdr::reporting;

using ::testing::_;
using ::testing::ByMove;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArg;
using ::testing::WithArgs;

struct FeaturedAndPolicy {
  bool featured;
  bool policy;
};

class DaemonTestFixture : public ::testing::TestWithParam<FeaturedAndPolicy> {
 protected:
  void SetUp() override {
    agent_plugin_ = std::make_unique<MockPlugin>();
    agent_plugin_ref_ = agent_plugin_.get();

    process_plugin_ = std::make_unique<MockPlugin>();
    process_plugin_ref_ = process_plugin_.get();

    plugin_factory_ = std::make_unique<MockPluginFactory>();
    plugin_factory_ref = plugin_factory_.get();

    message_sender_ = base::MakeRefCounted<MockMessageSender>();
    process_cache_ = base::MakeRefCounted<MockProcessCache>();
    policies_features_broker_ =
        base::MakeRefCounted<MockPoliciesFeaturesBroker>();

    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;

    struct Inject inject = {
        std::move(plugin_factory_),
        std::make_unique<MetricsLibrary>(),
        message_sender_,
        process_cache_,
        policies_features_broker_,
        new dbus::MockBus(options),
    };

    daemon_ = std::make_unique<Daemon>(std::move(inject));
  }

  void CallBroker(bool first_run, bool policy, bool featured) {
    if (first_run) {
      EXPECT_CALL(*policies_features_broker_, StartAndBlockForSync);
    }
    EXPECT_CALL(*policies_features_broker_, GetDeviceReportXDREventsPolicy)
        .WillOnce(Return(policy));
    EXPECT_CALL(*policies_features_broker_,
                GetFeature(PoliciesFeaturesBroker::Feature::
                               kCrOSLateBootSecagentdXDRReporting))
        .WillOnce(Return(featured));
  }

  void CallDaemon(bool first_run) {
    if (first_run) {
      daemon_->OnEventLoopStarted();
    }
    // Simulate broker callback.
    daemon_->CheckPolicyAndFeature();
  }

  void ExpectReporting(bool isReporting) {
    EXPECT_EQ(isReporting, daemon_->reporting_events_);
  }

  void EnableReporting() {
    // Check agent plugin.
    EXPECT_CALL(*plugin_factory_ref, CreateAgentPlugin)
        .WillOnce(WithArg<3>(Invoke([this](base::OnceCallback<void()> cb) {
          std::move(cb).Run();
          return std::move(agent_plugin_);
        })));
    EXPECT_CALL(*agent_plugin_ref_, Activate);

    // Check process plugin.
    EXPECT_CALL(*plugin_factory_ref,
                Create(Types::Plugin::kProcess, _, _, _, _))
        .WillOnce(Return(ByMove(std::move(process_plugin_))));
    EXPECT_CALL(*process_plugin_ref_, Activate);
  }

  std::unique_ptr<Daemon> daemon_;
  std::unique_ptr<MockPluginFactory> plugin_factory_;
  std::unique_ptr<MockPlugin> agent_plugin_;
  MockPlugin* agent_plugin_ref_;
  std::unique_ptr<MockPlugin> process_plugin_;
  MockPlugin* process_plugin_ref_;
  MockPluginFactory* plugin_factory_ref;
  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<MockProcessCache> process_cache_;
  scoped_refptr<MockPoliciesFeaturesBroker> policies_features_broker_;
};

TEST_F(DaemonTestFixture, TestReportingEnabled) {
  CallBroker(/*first_run*/ true, /*policy*/ true, /*featured*/ true);
  EnableReporting();

  CallDaemon(/*first_run*/ true);
  ExpectReporting(true);
}

TEST_F(DaemonTestFixture, TestEnabledToDisabled) {
  // Enable reporting.
  CallBroker(/*first_run*/ true, /*policy*/ true, /*featured*/ true);
  EnableReporting();
  CallDaemon(/*first_run*/ true);
  ExpectReporting(true);

  // Disable reporting.
  CallBroker(/*first_run*/ false, /*policy*/ false, /*featured*/ false);
  CallDaemon(/*first_run*/ false);
  ExpectReporting(false);
}

TEST_F(DaemonTestFixture, TestDisabledToEnabled) {
  // Disable reporting.
  CallBroker(/*first_run*/ true, /*policy*/ false, /*featured*/ false);
  CallDaemon(/*first_run*/ true);
  ExpectReporting(false);

  // Enable reporting.
  CallBroker(/*first_run*/ false, /*policy*/ true, /*featured*/ true);
  EnableReporting();
  CallDaemon(/*first_run*/ false);
  ExpectReporting(true);
}

TEST_P(DaemonTestFixture, TestReportingDisabled) {
  const FeaturedAndPolicy param = GetParam();

  CallBroker(/*first_run*/ true, param.policy, param.featured);

  CallDaemon(/*first_run*/ true);
  ExpectReporting(false);
}

INSTANTIATE_TEST_SUITE_P(
    DaemonTestFixture,
    DaemonTestFixture,
    // {featured, policy}
    ::testing::ValuesIn<FeaturedAndPolicy>(
        {{false, false}, {false, true}, {true, false}}),
    [](const ::testing::TestParamInfo<DaemonTestFixture::ParamType>& info) {
      std::string featured =
          info.param.featured ? "FeaturedEnabled" : "FeaturedDisabled";
      std::string policy =
          info.param.policy ? "PolicyEnabled" : "PolicyDisabled";

      return base::StringPrintf("%s_%s", featured.c_str(), policy.c_str());
    });

}  // namespace secagentd::testing
