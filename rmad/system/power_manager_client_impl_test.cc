// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/power_manager_client_impl.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <power_manager/dbus-proxy-mocks.h>

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class PowerManagerClientTest : public testing::Test {
 public:
  PowerManagerClientTest() = default;
  ~PowerManagerClientTest() override = default;
};

TEST_F(PowerManagerClientTest, Restart_Success) {
  auto mock_power_manager_proxy =
      std::make_unique<StrictMock<org::chromium::PowerManagerProxyMock>>();
  EXPECT_CALL(*mock_power_manager_proxy, RequestRestart(_, _, _, _))
      .WillOnce(Return(true));

  auto power_manager_client = std::make_unique<PowerManagerClientImpl>(
      std::move(mock_power_manager_proxy));

  EXPECT_TRUE(power_manager_client->Restart());
}

TEST_F(PowerManagerClientTest, Restart_Failed) {
  auto mock_power_manager_proxy =
      std::make_unique<StrictMock<org::chromium::PowerManagerProxyMock>>();
  EXPECT_CALL(*mock_power_manager_proxy, RequestRestart(_, _, _, _))
      .WillOnce(Return(false));

  auto power_manager_client = std::make_unique<PowerManagerClientImpl>(
      std::move(mock_power_manager_proxy));

  EXPECT_FALSE(power_manager_client->Restart());
}

TEST_F(PowerManagerClientTest, Shutdown_Success) {
  auto mock_power_manager_proxy =
      std::make_unique<StrictMock<org::chromium::PowerManagerProxyMock>>();
  EXPECT_CALL(*mock_power_manager_proxy, RequestShutdown(_, _, _, _))
      .WillOnce(Return(true));

  auto power_manager_client = std::make_unique<PowerManagerClientImpl>(
      std::move(mock_power_manager_proxy));

  EXPECT_TRUE(power_manager_client->Shutdown());
}

TEST_F(PowerManagerClientTest, Shutdown_Failed) {
  auto mock_power_manager_proxy =
      std::make_unique<StrictMock<org::chromium::PowerManagerProxyMock>>();
  EXPECT_CALL(*mock_power_manager_proxy, RequestShutdown(_, _, _, _))
      .WillOnce(Return(false));

  auto power_manager_client = std::make_unique<PowerManagerClientImpl>(
      std::move(mock_power_manager_proxy));

  EXPECT_FALSE(power_manager_client->Shutdown());
}

}  // namespace rmad
