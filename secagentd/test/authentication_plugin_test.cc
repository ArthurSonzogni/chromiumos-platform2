// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>

#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "dbus/mock_bus.h"
#include "gmock/gmock.h"  // IWYU pragma: keep
#include "gtest/gtest.h"
#include "secagentd/plugins.h"
#include "secagentd/test/mock_device_user.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"

namespace secagentd::testing {

namespace pb = cros_xdr::reporting;

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArg;
using ::testing::WithArgs;

class AuthenticationPluginTestFixture : public ::testing::Test {
 protected:
  AuthenticationPluginTestFixture()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  void SetUp() override {
    message_sender_ = base::MakeRefCounted<MockMessageSender>();
    device_user_ = base::MakeRefCounted<MockDeviceUser>();
    process_cache_ = base::MakeRefCounted<MockProcessCache>();
    policies_features_broker_ =
        base::MakeRefCounted<MockPoliciesFeaturesBroker>();
    plugin_factory_ = std::make_unique<PluginFactory>();
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);
  }
  void TearDown() override { task_environment_.RunUntilIdle(); }

  void CreateAuthenticationPlugin(base::RunLoop* run_loop) {
    plugin_ = plugin_factory_->Create(
        Types::Plugin::kAuthenticate, message_sender_, process_cache_,
        policies_features_broker_, device_user_, -1);
    EXPECT_NE(nullptr, plugin_);
  }

  base::test::TaskEnvironment task_environment_;
  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<dbus::MockBus> bus_;
  // Unused in authentication plugin.
  scoped_refptr<MockProcessCache> process_cache_;
  scoped_refptr<MockPoliciesFeaturesBroker> policies_features_broker_;
  scoped_refptr<MockDeviceUser> device_user_;
  std::unique_ptr<PluginFactory> plugin_factory_;
  std::unique_ptr<PluginInterface> plugin_;
};

}  // namespace secagentd::testing
