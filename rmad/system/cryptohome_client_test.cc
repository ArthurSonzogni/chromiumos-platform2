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

namespace fake {

// Tests for |FakeCryptohomeClient|.
class FakeCryptohomeClientTest : public testing::Test {
 public:
  FakeCryptohomeClientTest() = default;
  ~FakeCryptohomeClientTest() override = default;

  void SetIsEnrolled() {
    const base::FilePath is_enrolled_file_path =
        temp_dir_.GetPath().AppendASCII(kIsEnrolledFilePath);
    brillo::TouchFile(is_enrolled_file_path);
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

TEST_F(FakeCryptohomeClientTest, IsEnrolled_Enrolled) {
  SetIsEnrolled();
  EXPECT_TRUE(fake_cryptohome_client_->IsEnrolled());
}

TEST_F(FakeCryptohomeClientTest, IsEnrolled_NotEnrolled) {
  EXPECT_FALSE(fake_cryptohome_client_->IsEnrolled());
}

TEST_F(FakeCryptohomeClientTest, HasFwmp_Enrolled) {
  SetIsEnrolled();
  EXPECT_TRUE(fake_cryptohome_client_->HasFwmp());
}

TEST_F(FakeCryptohomeClientTest, HasFwmp_NotEnrolled) {
  EXPECT_FALSE(fake_cryptohome_client_->HasFwmp());
}

}  // namespace fake

}  // namespace rmad
