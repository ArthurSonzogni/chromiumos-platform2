// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

TEST_F(TpmManagerClientTest, RoVerification_NotTriggered) {
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
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_NOT_TRIGGERED);
}

TEST_F(TpmManagerClientTest, RoVerification_PassUnverifiedGbb) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_ro_verification_status(tpm_manager::RO_STATUS_PASS_UNVERIFIED_GBB);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_PASS);
}

TEST_F(TpmManagerClientTest, RoVerification_Pass) {
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
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_PASS);
}

TEST_F(TpmManagerClientTest, RoVerification_Fail) {
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
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_FAIL);
}

TEST_F(TpmManagerClientTest, RoVerification_Unsupported) {
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
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_UNSUPPORTED);
}

TEST_F(TpmManagerClientTest, RoVerification_UnsupportedNotTriggered) {
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
            RMAD_RO_VERIFICATION_UNSUPPORTED_NOT_TRIGGERED);
}

TEST_F(TpmManagerClientTest, RoVerification_UnsupportedTriggered) {
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
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_UNSUPPORTED_TRIGGERED);
}

TEST_F(TpmManagerClientTest, RoVerification_V2Success) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);

  // Ti50 verify RO on every boot so this should not trigger Shimless RMA.
  reply.set_ro_verification_status(tpm_manager::RO_STATUS_V2_SUCCESS);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_UNSUPPORTED);
}

TEST_F(TpmManagerClientTest, RoVerification_V2BoardIdMismatch) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);

  // Ti50 verify RO on every boot so this should not trigger Shimless RMA.
  reply.set_ro_verification_status(tpm_manager::RO_STATUS_V2_BOARD_ID_MISMATCH);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_UNSUPPORTED);
}

TEST_F(TpmManagerClientTest, RoVerification_V2NonZeroGbb) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);

  // Ti50 verify RO on every boot so this should not trigger Shimless RMA.
  reply.set_ro_verification_status(
      tpm_manager::RO_STATUS_V2_NON_ZERO_GBB_FLAGS);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_UNSUPPORTED);
}

TEST_F(TpmManagerClientTest, RoVerification_V2NotProvisioned) {
  tpm_manager::GetRoVerificationStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);

  // Ti50 verify RO on every boot so this should not trigger Shimless RMA.
  reply.set_ro_verification_status(
      tpm_manager::RO_STATUS_V2_SETTING_NOT_PROVISIONED);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetRoVerificationStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  RoVerificationStatus ro_verification_status;
  EXPECT_TRUE(
      tpm_manager_client->GetRoVerificationStatus(&ro_verification_status));
  EXPECT_EQ(ro_verification_status, RMAD_RO_VERIFICATION_UNSUPPORTED);
}

TEST_F(TpmManagerClientTest, RoVerification_DBusError) {
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

TEST_F(TpmManagerClientTest, RoVerification_TpmManagerError) {
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

TEST_F(TpmManagerClientTest, GetGscVersion_NotGsc) {
  tpm_manager::GetVersionInfoReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_gsc_version(tpm_manager::GSC_VERSION_NOT_GSC);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetVersionInfo(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  GscVersion gsc_version;
  EXPECT_TRUE(tpm_manager_client->GetGscVersion(&gsc_version));
  EXPECT_EQ(gsc_version, GscVersion::GSC_VERSION_NOT_GSC);
}

TEST_F(TpmManagerClientTest, GetGscVersion_Cr50) {
  tpm_manager::GetVersionInfoReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_gsc_version(tpm_manager::GSC_VERSION_CR50);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetVersionInfo(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  GscVersion gsc_version;
  EXPECT_TRUE(tpm_manager_client->GetGscVersion(&gsc_version));
  EXPECT_EQ(gsc_version, GscVersion::GSC_VERSION_CR50);
}

TEST_F(TpmManagerClientTest, GetGscVersion_Ti50) {
  tpm_manager::GetVersionInfoReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_gsc_version(tpm_manager::GSC_VERSION_TI50);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetVersionInfo(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  GscVersion gsc_version;
  EXPECT_TRUE(tpm_manager_client->GetGscVersion(&gsc_version));
  EXPECT_EQ(gsc_version, GscVersion::GSC_VERSION_TI50);
}

TEST_F(TpmManagerClientTest, GetGscVersion_DBusError) {
  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetVersionInfo(_, _, _, _))
      .WillRepeatedly(Return(false));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  GscVersion gsc_version;
  EXPECT_FALSE(tpm_manager_client->GetGscVersion(&gsc_version));
}

TEST_F(TpmManagerClientTest, GetGscVersion_TpmManagerError) {
  tpm_manager::GetVersionInfoReply reply;
  reply.set_status(tpm_manager::STATUS_DEVICE_ERROR);

  auto mock_tpm_manager_proxy =
      std::make_unique<StrictMock<org::chromium::TpmManagerProxyMock>>();
  EXPECT_CALL(*mock_tpm_manager_proxy, GetVersionInfo(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto tpm_manager_client =
      std::make_unique<TpmManagerClientImpl>(std::move(mock_tpm_manager_proxy));

  GscVersion gsc_version;
  EXPECT_FALSE(tpm_manager_client->GetGscVersion(&gsc_version));
}

}  // namespace rmad
