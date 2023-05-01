// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/secagent.h"
#include <absl/status/status.h>
#include <sysexits.h>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/task_environment.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"
#include "gmock/gmock.h"  // IWYU pragma: keep
#include "gtest/gtest.h"
#include "metrics/metrics_library.h"
#include "secagentd/common.h"
#include "secagentd/plugins.h"
#include "secagentd/test/mock_device_user.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_plugin_factory.h"
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"
#include "session_manager/dbus-proxies.h"
#include "session_manager/dbus-proxy-mocks.h"

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

class SecAgentTestFixture : public ::testing::TestWithParam<FeaturedAndPolicy> {
 protected:
  SecAgentTestFixture()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
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
    device_user_ = base::MakeRefCounted<MockDeviceUser>();

    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);

    secagent_ = std::make_unique<SecAgent>(
        base::BindOnce(&SecAgentTestFixture::DaemonCb, base::Unretained(this)),
        message_sender_, process_cache_, device_user_,
        std::move(plugin_factory_),
        // attestation and tpm proxies.
        nullptr /* Attestation */, nullptr /* Tpm */,
        nullptr /* PlatformFeatures */, 0, 0, 300, 120);
    secagent_->policies_features_broker_ = this->policies_features_broker_;
  }

  void TearDown() override { task_environment_.RunUntilIdle(); }

  void DaemonCb(int rv) {
    EXPECT_EQ(expected_exit_code_, rv);
    run_loop_ptr_->Quit();
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

  void ExpectReporting(bool isReporting) {
    EXPECT_EQ(isReporting, secagent_->reporting_events_);
  }

  void EnableReporting() {
    EXPECT_CALL(*device_user_, RegisterSessionChangeHandler);

    // Check agent plugin.
    EXPECT_CALL(*plugin_factory_ref, CreateAgentPlugin)
        .WillOnce(WithArg<4>(Invoke([this](base::OnceCallback<void()> cb) {
          std::move(cb).Run();
          return std::move(agent_plugin_);
        })));
    EXPECT_CALL(*agent_plugin_ref_, Activate)
        .WillOnce(Return(absl::OkStatus()));

    // Check process plugin.
    EXPECT_CALL(*plugin_factory_ref,
                Create(Types::Plugin::kProcess, _, _, _, _, _))
        .WillOnce(Return(ByMove(std::move(process_plugin_))));
    EXPECT_CALL(*process_plugin_ref_, Activate)
        .WillOnce(Return(absl::OkStatus()));
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<SecAgent> secagent_;
  std::unique_ptr<MockPluginFactory> plugin_factory_;
  std::unique_ptr<MockPlugin> agent_plugin_;
  MockPlugin* agent_plugin_ref_;
  std::unique_ptr<MockPlugin> process_plugin_;
  MockPlugin* process_plugin_ref_;
  MockPluginFactory* plugin_factory_ref;
  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<MockProcessCache> process_cache_;
  scoped_refptr<MockPoliciesFeaturesBroker> policies_features_broker_;
  scoped_refptr<MockDeviceUser> device_user_;
  scoped_refptr<dbus::MockBus> bus_;
  base::RunLoop* run_loop_ptr_;
  int expected_exit_code_;
};

TEST_F(SecAgentTestFixture, TestReportingEnabled) {
  EXPECT_CALL(*message_sender_, Initialize).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*process_cache_, InitializeFilter);
  CallBroker(/*first_run*/ true, /*policy*/ true, /*featured*/ true);
  EnableReporting();

  secagent_->Activate();
  secagent_->CheckPolicyAndFeature();
  ExpectReporting(true);
}

TEST_F(SecAgentTestFixture, TestEnabledToDisabled) {
  expected_exit_code_ = EX_OK;
  EXPECT_CALL(*message_sender_, Initialize).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*process_cache_, InitializeFilter);

  // Enable reporting.
  CallBroker(/*first_run*/ true, /*policy*/ true, /*featured*/ true);
  EnableReporting();
  secagent_->Activate();
  secagent_->CheckPolicyAndFeature();
  ExpectReporting(true);

  // Disable reporting.
  CallBroker(/*first_run*/ false, /*policy*/ false, /*featured*/ false);
  base::RunLoop run_loop = base::RunLoop();
  run_loop_ptr_ = &run_loop;
  secagent_->CheckPolicyAndFeature();
  ExpectReporting(false);
  run_loop.Run();
}

TEST_F(SecAgentTestFixture, TestDisabledToEnabled) {
  EXPECT_CALL(*message_sender_, Initialize).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*process_cache_, InitializeFilter);

  // Disable reporting.
  CallBroker(/*first_run*/ true, /*policy*/ false, /*featured*/ false);
  secagent_->Activate();
  secagent_->CheckPolicyAndFeature();
  ExpectReporting(false);

  // Enable reporting.
  CallBroker(/*first_run*/ false, /*policy*/ true, /*featured*/ true);
  EnableReporting();
  secagent_->CheckPolicyAndFeature();
  ExpectReporting(true);
}

TEST_F(SecAgentTestFixture, TestFailedInitialization) {
  expected_exit_code_ = EX_SOFTWARE;
  EXPECT_CALL(*message_sender_, Initialize)
      .WillOnce(Return(absl::InternalError(
          "InitializeQueues: Report queue failed to create")));
  ExpectReporting(false);

  base::RunLoop run_loop = base::RunLoop();
  run_loop_ptr_ = &run_loop;
  secagent_->Activate();
  run_loop.Run();
}

TEST_F(SecAgentTestFixture, TestFailedPluginCreation) {
  expected_exit_code_ = EX_SOFTWARE;
  EXPECT_CALL(*message_sender_, Initialize).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*process_cache_, InitializeFilter);
  EXPECT_CALL(*device_user_, RegisterSessionChangeHandler);

  EXPECT_CALL(*plugin_factory_ref, CreateAgentPlugin).WillOnce(Return(nullptr));

  CallBroker(/*first_run*/ true, /*policy*/ true, /*featured*/ true);
  ExpectReporting(false);
  base::RunLoop run_loop = base::RunLoop();
  run_loop_ptr_ = &run_loop;
  secagent_->Activate();
  secagent_->CheckPolicyAndFeature();
  run_loop.Run();
}

TEST_F(SecAgentTestFixture, TestFailedPluginActivation) {
  expected_exit_code_ = EX_SOFTWARE;
  EXPECT_CALL(*message_sender_, Initialize).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*process_cache_, InitializeFilter);
  EXPECT_CALL(*device_user_, RegisterSessionChangeHandler);
  EXPECT_CALL(*plugin_factory_ref, CreateAgentPlugin)
      .WillOnce(WithArg<4>(Invoke([this](base::OnceCallback<void()> cb) {
        std::move(cb).Run();
        return std::move(agent_plugin_);
      })));
  EXPECT_CALL(*agent_plugin_ref_, Activate).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*plugin_factory_ref,
              Create(Types::Plugin::kProcess, _, _, _, _, _))
      .WillOnce(Return(ByMove(std::move(process_plugin_))));

  EXPECT_CALL(*process_plugin_ref_, Activate)
      .WillOnce(
          Return(absl::InternalError("Process BPF program loading error.")));

  CallBroker(/*first_run*/ true, /*policy*/ true, /*featured*/ true);
  ExpectReporting(false);
  base::RunLoop run_loop = base::RunLoop();
  run_loop_ptr_ = &run_loop;
  secagent_->Activate();
  secagent_->CheckPolicyAndFeature();
  run_loop.Run();
}

TEST_P(SecAgentTestFixture, TestReportingDisabled) {
  EXPECT_CALL(*message_sender_, Initialize).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*process_cache_, InitializeFilter);
  const FeaturedAndPolicy param = GetParam();

  CallBroker(/*first_run*/ true, param.policy, param.featured);

  secagent_->Activate();
  secagent_->CheckPolicyAndFeature();
  ExpectReporting(false);
}

INSTANTIATE_TEST_SUITE_P(
    SecAgentTestFixture,
    SecAgentTestFixture,
    // {featured, policy}
    ::testing::ValuesIn<FeaturedAndPolicy>(
        {{false, false}, {false, true}, {true, false}}),
    [](const ::testing::TestParamInfo<SecAgentTestFixture::ParamType>& info) {
      std::string featured =
          info.param.featured ? "FeaturedEnabled" : "FeaturedDisabled";
      std::string policy =
          info.param.policy ? "PolicyEnabled" : "PolicyDisabled";

      return base::StringPrintf("%s_%s", featured.c_str(), policy.c_str());
    });

}  // namespace secagentd::testing
