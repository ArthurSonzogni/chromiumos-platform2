// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <user_data_auth-client-test/user_data_auth/dbus-proxy-mocks.h>

#include "rmad/system/cryptohome_client_impl.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class CryptohomeClientTest : public testing::Test {
 public:
  CryptohomeClientTest() = default;
  ~CryptohomeClientTest() override = default;
};

TEST_F(CryptohomeClientTest, Fwmp_Exist_Enrolled) {
  user_data_auth::FirmwareManagementParameters fwmp;
  fwmp.set_flags(0x1);
  user_data_auth::GetFirmwareManagementParametersReply reply;
  reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  *reply.mutable_fwmp() = fwmp;

  auto mock_cryptohome_proxy = std::make_unique<
      StrictMock<org::chromium::InstallAttributesInterfaceProxyMock>>();
  EXPECT_CALL(*mock_cryptohome_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto cryptohome_client =
      std::make_unique<CryptohomeClientImpl>(std::move(mock_cryptohome_proxy));

  EXPECT_TRUE(cryptohome_client->HasFwmp());
  EXPECT_TRUE(cryptohome_client->IsEnrolled());
}

TEST_F(CryptohomeClientTest, Fwmp_Exist_Unenrolled) {
  user_data_auth::FirmwareManagementParameters fwmp;
  fwmp.set_flags(0x0);
  user_data_auth::GetFirmwareManagementParametersReply reply;
  reply.set_error(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  *reply.mutable_fwmp() = fwmp;

  auto mock_cryptohome_proxy = std::make_unique<
      StrictMock<org::chromium::InstallAttributesInterfaceProxyMock>>();
  EXPECT_CALL(*mock_cryptohome_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto cryptohome_client =
      std::make_unique<CryptohomeClientImpl>(std::move(mock_cryptohome_proxy));

  EXPECT_TRUE(cryptohome_client->HasFwmp());
  EXPECT_FALSE(cryptohome_client->IsEnrolled());
}

TEST_F(CryptohomeClientTest, Fwmp_Nonexist) {
  user_data_auth::GetFirmwareManagementParametersReply reply;
  reply.set_error(
      user_data_auth::CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID);

  auto mock_cryptohome_proxy = std::make_unique<
      StrictMock<org::chromium::InstallAttributesInterfaceProxyMock>>();
  EXPECT_CALL(*mock_cryptohome_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto cryptohome_client =
      std::make_unique<CryptohomeClientImpl>(std::move(mock_cryptohome_proxy));

  EXPECT_FALSE(cryptohome_client->HasFwmp());
  EXPECT_FALSE(cryptohome_client->IsEnrolled());
}

TEST_F(CryptohomeClientTest, Proxy_Failed) {
  auto mock_cryptohome_proxy = std::make_unique<
      StrictMock<org::chromium::InstallAttributesInterfaceProxyMock>>();
  EXPECT_CALL(*mock_cryptohome_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(Return(false));

  auto cryptohome_client =
      std::make_unique<CryptohomeClientImpl>(std::move(mock_cryptohome_proxy));

  EXPECT_FALSE(cryptohome_client->HasFwmp());
  EXPECT_FALSE(cryptohome_client->IsEnrolled());
}

}  // namespace rmad
