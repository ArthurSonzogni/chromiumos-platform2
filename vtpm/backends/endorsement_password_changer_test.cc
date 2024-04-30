// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/endorsement_password_changer.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>
#include <trunks/tpm_generated.h>
#include "trunks/password_authorization_delegate.h"

namespace vtpm {

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

constexpr char kVirtualPassword[] = "virtual password";
constexpr char kRealPassword[] = "real password";
constexpr trunks::TPMI_SH_POLICY kFakePolicySession = 333;
constexpr trunks::UINT32 kFakeExpiration = 100;

}  // namespace

class EndorsementPasswordChangerTest : public testing::Test {
 public:
 protected:
  StrictMock<org::chromium::TpmManagerProxyMock> mock_tpm_manager_;
  EndorsementPasswordChanger password_changer_{&mock_tpm_manager_,
                                               kVirtualPassword};
};

namespace {

TEST_F(EndorsementPasswordChangerTest, PasswordChanged) {
  std::string virtual_command;
  trunks::PasswordAuthorizationDelegate virtual_password_authorization(
      kVirtualPassword);
  ASSERT_EQ(
      trunks::Tpm::SerializeCommand_PolicySecret(
          trunks::TPM_RH_ENDORSEMENT, "entity name placeholder",
          kFakePolicySession, "policy session name placeholder",
          trunks::Make_TPM2B_DIGEST("nonce tpm placeholder"),
          trunks::Make_TPM2B_DIGEST("cp hash a placeholder"),
          trunks::Make_TPM2B_DIGEST("policy ref placeholder"), kFakeExpiration,
          &virtual_command, &virtual_password_authorization),
      trunks::TPM_RC_SUCCESS);

  std::string real_command;
  trunks::PasswordAuthorizationDelegate real_password_authorization(
      kRealPassword);
  ASSERT_EQ(trunks::Tpm::SerializeCommand_PolicySecret(
                trunks::TPM_RH_ENDORSEMENT, "entity name placeholder",
                kFakePolicySession, "policy session name placeholder",
                trunks::Make_TPM2B_DIGEST("nonce tpm placeholder"),
                trunks::Make_TPM2B_DIGEST("cp hash a placeholder"),
                trunks::Make_TPM2B_DIGEST("policy ref placeholder"),
                kFakeExpiration, &real_command, &real_password_authorization),
            trunks::TPM_RC_SUCCESS);

  tpm_manager::GetTpmStatusReply reply;
  reply.set_status(tpm_manager::STATUS_SUCCESS);
  reply.set_enabled(true);
  reply.set_owned(true);
  reply.mutable_local_data()->set_endorsement_password(kRealPassword);
  EXPECT_CALL(mock_tpm_manager_, GetTpmStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  EXPECT_EQ(password_changer_.Change(virtual_command), trunks::TPM_RC_SUCCESS);
  EXPECT_EQ(virtual_command.size(), real_command.size());
  EXPECT_EQ(virtual_command, real_command);
}

}  // namespace

}  // namespace vtpm
