// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/device_management_client_impl.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "device_management-client-test/device_management/dbus-proxy-mocks.h"
#include "device_management/proto_bindings/device_management_interface.pb.h"
#include "rmad/constants.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class DeviceManagementClientTest : public testing::Test {
 public:
  DeviceManagementClientTest() = default;
  ~DeviceManagementClientTest() override = default;
};

TEST_F(DeviceManagementClientTest, Fwmp_Exist_CcdBlocked) {
  device_management::FirmwareManagementParameters fwmp;
  fwmp.set_flags(0x40);
  device_management::GetFirmwareManagementParametersReply reply;
  reply.set_error(device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET);
  *reply.mutable_fwmp() = fwmp;

  auto mock_device_management_proxy =
      std::make_unique<StrictMock<org::chromium::DeviceManagementProxyMock>>();
  EXPECT_CALL(*mock_device_management_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));
  auto device_management_client = std::make_unique<DeviceManagementClientImpl>(
      std::move(mock_device_management_proxy));

  EXPECT_TRUE(device_management_client->IsCcdBlocked());
}

TEST_F(DeviceManagementClientTest, Fwmp_Exist_CcdNotBlocked) {
  device_management::FirmwareManagementParameters fwmp;
  fwmp.set_flags(0x0);
  device_management::GetFirmwareManagementParametersReply reply;
  reply.set_error(device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET);
  *reply.mutable_fwmp() = fwmp;

  auto mock_device_management_proxy =
      std::make_unique<StrictMock<org::chromium::DeviceManagementProxyMock>>();
  EXPECT_CALL(*mock_device_management_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));
  auto device_management_client = std::make_unique<DeviceManagementClientImpl>(
      std::move(mock_device_management_proxy));

  EXPECT_FALSE(device_management_client->IsCcdBlocked());
}

TEST_F(DeviceManagementClientTest, Fwmp_Nonexist) {
  device_management::GetFirmwareManagementParametersReply reply;
  reply.set_error(
      device_management::
          DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID);

  auto mock_device_management_proxy =
      std::make_unique<StrictMock<org::chromium::DeviceManagementProxyMock>>();
  EXPECT_CALL(*mock_device_management_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto device_management_client = std::make_unique<DeviceManagementClientImpl>(
      std::move(mock_device_management_proxy));

  EXPECT_FALSE(device_management_client->IsCcdBlocked());
}

TEST_F(DeviceManagementClientTest, Proxy_Failed) {
  auto mock_device_management_proxy =
      std::make_unique<StrictMock<org::chromium::DeviceManagementProxyMock>>();
  EXPECT_CALL(*mock_device_management_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(Return(false));

  auto device_management_client = std::make_unique<DeviceManagementClientImpl>(
      std::move(mock_device_management_proxy));

  EXPECT_FALSE(device_management_client->IsCcdBlocked());
}

}  // namespace rmad
