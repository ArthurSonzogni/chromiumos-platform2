// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_tpm_manager_client.h"
#include "rmad/system/tpm_manager_client_impl.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "rmad/constants.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

// Tests for |TpmManagerClientImpl|.
class TpmManagerClientTest : public testing::Test {
 public:
  TpmManagerClientTest() = default;
  ~TpmManagerClientTest() override = default;
};

TEST_F(TpmManagerClientTest, RoVerificationNotTriggered) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_ro_verification_status(tpm_manager::RO_STATUS_NOT_TRIGGERED);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RoVerificationStatus::NOT_TRIGGERED);
}

TEST_F(TpmManagerClientTest, RoVerificationPass) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_ro_verification_status(tpm_manager::RO_STATUS_PASS);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RoVerificationStatus::PASS);
}

TEST_F(TpmManagerClientTest, RoVerificationFail) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_ro_verification_status(tpm_manager::RO_STATUS_FAIL);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RoVerificationStatus::FAIL);
}

TEST_F(TpmManagerClientTest, RoVerificationUnsupported) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_ro_verification_status(tpm_manager::RO_STATUS_UNSUPPORTED);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RoVerificationStatus::UNSUPPORTED);
}

TEST_F(TpmManagerClientTest, RoVerificationUnsupportedNotTriggered) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_ro_verification_status(
      tpm_manager::RO_STATUS_UNSUPPORTED_NOT_TRIGGERED);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status,
            RoVerificationStatus::UNSUPPORTED_NOT_TRIGGERED);
}

TEST_F(TpmManagerClientTest, RoVerificationUnsupportedTriggered) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_ro_verification_status(
      tpm_manager::RO_STATUS_UNSUPPORTED_TRIGGERED);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status,
            RoVerificationStatus::UNSUPPORTED_TRIGGERED);
}

TEST_F(TpmManagerClientTest, RoVerificationDBusError) {
  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(Return(false));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_FALSE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
}

TEST_F(TpmManagerClientTest, RoVerificationTpmManagerError) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_DEVICE_ERROR);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_FALSE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
}

namespace fake {

// Tests for |FakeTpmManager|.
class FakeTpmManagerClientTest : public testing::Test {
 public:
  FakeTpmManagerClientTest() = default;
  ~FakeTpmManagerClientTest() override = default;

  bool WriteRoVerificationStatus(const std::string& str) {
    base::FilePath ro_verification_status_file_path =
        temp_dir_.GetPath().AppendASCII(kRoVerificationStatusFilePath);
    return base::WriteFile(ro_verification_status_file_path, str);
  }

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_tpm_manager_client_ =
        std::make_unique<FakeTpmManagerClient>(temp_dir_.GetPath());
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<FakeTpmManagerClient> fake_tpm_manager_client_;
};

TEST_F(FakeTpmManagerClientTest, RoVerification_NotTriggered) {
  WriteRoVerificationStatus("NOT_TRIGGERED");
  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(fake_tpm_manager_client_->GetRoVerificationStatus(
      &ro_verification_status));
  EXPECT_EQ(ro_verification_status, RoVerificationStatus::NOT_TRIGGERED);
}

TEST_F(FakeTpmManagerClientTest, RoVerification_Pass) {
  WriteRoVerificationStatus("PASS");
  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(fake_tpm_manager_client_->GetRoVerificationStatus(
      &ro_verification_status));
  EXPECT_EQ(ro_verification_status, RoVerificationStatus::PASS);
}

TEST_F(FakeTpmManagerClientTest, RoVerification_Fail) {
  WriteRoVerificationStatus("FAIL");
  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(fake_tpm_manager_client_->GetRoVerificationStatus(
      &ro_verification_status));
  EXPECT_EQ(ro_verification_status, RoVerificationStatus::FAIL);
}

TEST_F(FakeTpmManagerClientTest, RoVerification_Unsupported) {
  WriteRoVerificationStatus("UNSUPPORTED");
  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(fake_tpm_manager_client_->GetRoVerificationStatus(
      &ro_verification_status));
  EXPECT_EQ(ro_verification_status, RoVerificationStatus::UNSUPPORTED);
}

TEST_F(FakeTpmManagerClientTest, RoVerification_UnsupportedNotTriggered) {
  WriteRoVerificationStatus("UNSUPPORTED_NOT_TRIGGERED");
  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(fake_tpm_manager_client_->GetRoVerificationStatus(
      &ro_verification_status));
  EXPECT_EQ(ro_verification_status,
            RoVerificationStatus::UNSUPPORTED_NOT_TRIGGERED);
}

TEST_F(FakeTpmManagerClientTest, RoVerification_UnsupportedTriggered) {
  WriteRoVerificationStatus("UNSUPPORTED_TRIGGERED");
  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(fake_tpm_manager_client_->GetRoVerificationStatus(
      &ro_verification_status));
  EXPECT_EQ(ro_verification_status,
            RoVerificationStatus::UNSUPPORTED_TRIGGERED);
}

TEST_F(FakeTpmManagerClientTest, RoVerification_ParseError) {
  WriteRoVerificationStatus("ABCDE");
  RoVerificationStatus ro_verification_status;
  EXPECT_FALSE(fake_tpm_manager_client_->GetRoVerificationStatus(
      &ro_verification_status));
}

TEST_F(FakeTpmManagerClientTest, RoVerification_NoFile) {
  RoVerificationStatus ro_verification_status;
  EXPECT_FALSE(fake_tpm_manager_client_->GetRoVerificationStatus(
      &ro_verification_status));
}

}  // namespace fake

}  // namespace rmad
