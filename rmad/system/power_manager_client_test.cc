// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class PowerManagerClientTest : public testing::Test {
 public:
  PowerManagerClientTest()
      : mock_bus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        mock_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            mock_bus_.get(),
            power_manager::kPowerManagerServiceName,
            dbus::ObjectPath(power_manager::kPowerManagerServicePath))) {}
  ~PowerManagerClientTest() override = default;

  dbus::MockObjectProxy* mock_object_proxy() const {
    return mock_object_proxy_.get();
  }

  PowerManagerClientImpl* power_manager_client() const {
    return power_manager_client_.get();
  }

  void SetUp() override {
    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(Return(mock_object_proxy_.get()));
    power_manager_client_ = std::make_unique<PowerManagerClientImpl>(mock_bus_);
  }

 private:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  std::unique_ptr<PowerManagerClientImpl> power_manager_client_;
};

TEST_F(PowerManagerClientTest, Restart_Success) {
  EXPECT_CALL(*mock_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });
  EXPECT_TRUE(power_manager_client()->Restart());
}

TEST_F(PowerManagerClientTest, Restart_Failed) {
  EXPECT_CALL(*mock_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });
  EXPECT_FALSE(power_manager_client()->Restart());
}

TEST_F(PowerManagerClientTest, Shutdown_Success) {
  EXPECT_CALL(*mock_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });
  EXPECT_TRUE(power_manager_client()->Shutdown());
}

TEST_F(PowerManagerClientTest, Shutdown_Failed) {
  EXPECT_CALL(*mock_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });
  EXPECT_FALSE(power_manager_client()->Shutdown());
}

namespace fake {

class FakePowerManagerClientTest : public testing::Test {
 public:
  FakePowerManagerClientTest() = default;
  ~FakePowerManagerClientTest() override = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_power_manager_client_ =
        std::make_unique<FakePowerManagerClient>(temp_dir_.GetPath());
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<FakePowerManagerClient> fake_power_manager_client_;
};

TEST_F(FakePowerManagerClientTest, Restart) {
  const base::FilePath reboot_request_file_path =
      temp_dir_.GetPath().AppendASCII(kRebootRequestFilePath);
  EXPECT_FALSE(base::PathExists(reboot_request_file_path));
  EXPECT_TRUE(fake_power_manager_client_->Restart());
  EXPECT_TRUE(base::PathExists(reboot_request_file_path));
}

TEST_F(FakePowerManagerClientTest, Shutdown) {
  const base::FilePath shutdown_request_file_path =
      temp_dir_.GetPath().AppendASCII(kShutdownRequestFilePath);
  EXPECT_FALSE(base::PathExists(shutdown_request_file_path));
  EXPECT_TRUE(fake_power_manager_client_->Shutdown());
  EXPECT_TRUE(base::PathExists(shutdown_request_file_path));
}

}  // namespace fake

}  // namespace rmad
