// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/cryptohome_client_impl.h"
#include "rmad/system/fake_cryptohome_client.h"

#include <cstdint>
#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <user_data_auth-client-test/user_data_auth/dbus-proxy-mocks.h>

#include "rmad/constants.h"

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

TEST_F(CryptohomeClientTest, Fwmp_Exist_CcdBlocked) {
  user_data_auth::FirmwareManagementParameters fwmp;
  fwmp.set_flags(0x40);
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

  EXPECT_TRUE(cryptohome_client->IsCcdBlocked());
}

TEST_F(CryptohomeClientTest, Fwmp_Exist_CcdNotBlocked) {
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

  EXPECT_FALSE(cryptohome_client->IsCcdBlocked());
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

  EXPECT_FALSE(cryptohome_client->IsCcdBlocked());
}

TEST_F(CryptohomeClientTest, Proxy_Failed) {
  auto mock_cryptohome_proxy = std::make_unique<
      StrictMock<org::chromium::InstallAttributesInterfaceProxyMock>>();
  EXPECT_CALL(*mock_cryptohome_proxy,
              GetFirmwareManagementParameters(_, _, _, _))
      .WillRepeatedly(Return(false));

  auto cryptohome_client =
      std::make_unique<CryptohomeClientImpl>(std::move(mock_cryptohome_proxy));

  EXPECT_FALSE(cryptohome_client->IsCcdBlocked());
}

namespace fake {

// Tests for |FakeCryptohomeClient|.
class FakeCryptohomeClientTest : public testing::Test {
 public:
  FakeCryptohomeClientTest() = default;
  ~FakeCryptohomeClientTest() override = default;

  void SetBlockCcd() {
    const base::FilePath block_ccd_file_path =
        temp_dir_.GetPath().AppendASCII(kBlockCcdFilePath);
    brillo::TouchFile(block_ccd_file_path);
  }

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_cryptohome_client_ =
        std::make_unique<FakeCryptohomeClient>(temp_dir_.GetPath());
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<FakeCryptohomeClient> fake_cryptohome_client_;
};

TEST_F(FakeCryptohomeClientTest, CcdBlocked) {
  SetBlockCcd();
  EXPECT_TRUE(fake_cryptohome_client_->IsCcdBlocked());
}

TEST_F(FakeCryptohomeClientTest, CcdNotBlocked) {
  EXPECT_FALSE(fake_cryptohome_client_->IsCcdBlocked());
}

}  // namespace fake

}  // namespace rmad
