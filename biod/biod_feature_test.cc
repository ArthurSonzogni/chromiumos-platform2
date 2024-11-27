// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_feature.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "biod/mock_session_state_manager.h"
#include "biod/updater/firmware_selector.h"
#include "featured/fake_platform_features.h"

namespace biod {

using testing::_;
using testing::A;
using testing::Return;

MATCHER_P(IsMember, name, "") {
  if (arg->GetMember() != name) {
    *result_listener << "has member " << arg->GetMember();
    return false;
  }
  return true;
}

class MockFirmwareSelector : public updater::FirmwareSelectorInterface {
 public:
  MOCK_METHOD(bool, IsBetaFirmwareAllowed, (), (const, override));
  MOCK_METHOD(void, AllowBetaFirmware, (bool enable), (override));
  MOCK_METHOD((base::expected<base::FilePath, FindFirmwareFileStatus>),
              FindFirmwareFile,
              (const std::string& board_name),
              (override));
};

class BiodFeatureTest : public testing::Test {
 public:
  BiodFeatureTest() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);

    proxy_ = new dbus::MockObjectProxy(
        bus_.get(), power_manager::kPowerManagerServiceName,
        dbus::ObjectPath(power_manager::kPowerManagerServicePath));

    session_manager_ = std::make_unique<MockSessionStateManager>();

    EXPECT_CALL(*bus_,
                GetObjectProxy(power_manager::kPowerManagerServiceName, _))
        .WillRepeatedly(Return(proxy_.get()));

    EXPECT_CALL(*bus_, GetOriginTaskRunner())
        .WillRepeatedly(
            Return(base::SequencedTaskRunner::GetCurrentDefault().get()));
  }

 protected:
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> proxy_;
  std::unique_ptr<MockSessionStateManager> session_manager_;
};

TEST_F(BiodFeatureTest, TestFirmwareDefaultProductionFirmwareSelected) {
  feature::FakePlatformFeatures features(bus_);
  auto selector = std::make_unique<MockFirmwareSelector>();

  EXPECT_CALL(*selector, IsBetaFirmwareAllowed).WillOnce(Return(false));
  EXPECT_CALL(*selector, AllowBetaFirmware).Times(0);
  // Make sure reboot was not requested.
  EXPECT_CALL(*proxy_,
              CallMethodAndBlock(IsMember(power_manager::kRequestRestartMethod),
                                 A<int>()))
      .Times(0);

  BiodFeature biod_feature(bus_, session_manager_.get(), &features,
                           std::move(selector));
}

TEST_F(BiodFeatureTest, TestForceTransitionToProductionFirmware) {
  feature::FakePlatformFeatures features(bus_);
  auto selector = std::make_unique<MockFirmwareSelector>();

  EXPECT_CALL(*selector, IsBetaFirmwareAllowed).WillOnce(Return(true));
  EXPECT_CALL(*selector, AllowBetaFirmware(false)).Times(1);
  // Make sure reboot was not requested.
  EXPECT_CALL(*proxy_,
              CallMethodAndBlock(IsMember(power_manager::kRequestRestartMethod),
                                 A<int>()))
      .Times(0);

  BiodFeature biod_feature(bus_, session_manager_.get(), &features,
                           std::move(selector));
}

}  // namespace biod
