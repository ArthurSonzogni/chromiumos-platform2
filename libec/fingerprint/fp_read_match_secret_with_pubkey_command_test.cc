// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_read_match_secret_with_pubkey_command.h"

#include <algorithm>
#include <cstring>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Return;

TEST(FpReadMatchSecretWithPubkeyCommand, IncorrectXSize) {
  const brillo::Blob pk_in_x(31, 1);
  const brillo::Blob pk_in_y(32, 2);

  auto cmd = FpReadMatchSecretWithPubkeyCommand::Create(1, pk_in_x, pk_in_y);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpReadMatchSecretWithPubkeyCommand, IncorrectYSize) {
  const brillo::Blob pk_in_x(32, 1);
  const brillo::Blob pk_in_y(31, 2);

  auto cmd = FpReadMatchSecretWithPubkeyCommand::Create(1, pk_in_x, pk_in_y);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpReadMatchSecretWithPubkeyCommand, FpReadMatchSecretWithPubkeyCommand) {
  const brillo::Blob pk_in_x(32, 1);
  const brillo::Blob pk_in_y(32, 2);

  auto cmd = FpReadMatchSecretWithPubkeyCommand::Create(1, pk_in_x, pk_in_y);
  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_READ_MATCH_SECRET_WITH_PUBKEY);
  EXPECT_EQ(cmd->Req()->fgr, 1);
  EXPECT_THAT(cmd->Req()->pubkey.x, ElementsAreArray(pk_in_x));
  EXPECT_THAT(cmd->Req()->pubkey.y, ElementsAreArray(pk_in_y));
}

// Mock the underlying EcCommand to test.
class FpReadMatchSecretWithPubkeyCommandTest : public testing::Test {
 public:
  class MockFpReadMatchSecretWithPubkeyCommand
      : public FpReadMatchSecretWithPubkeyCommand {
   public:
    using FpReadMatchSecretWithPubkeyCommand::
        FpReadMatchSecretWithPubkeyCommand;
    MOCK_METHOD(struct ec_response_fp_read_match_secret_with_pubkey*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(FpReadMatchSecretWithPubkeyCommandTest, Success) {
  const brillo::Blob pk_in_x(32, 1);
  const brillo::Blob pk_in_y(32, 2);
  const brillo::Blob pk_out_x(32, 3);
  const brillo::Blob pk_out_y(32, 4);
  const brillo::Blob encrypted_secret(32, 5);
  const brillo::Blob iv(16, 6);
  typedef ec_response_fp_read_match_secret_with_pubkey cmd_type;
  static_assert(sizeof(std::declval<cmd_type>().pubkey.x) == 32);
  static_assert(sizeof(std::declval<cmd_type>().pubkey.y) == 32);
  static_assert(sizeof(std::declval<cmd_type>().iv) == 16);
  static_assert(sizeof(std::declval<cmd_type>().enc_secret) == 32);

  auto mock_command = FpReadMatchSecretWithPubkeyCommand::Create<
      MockFpReadMatchSecretWithPubkeyCommand>(1, pk_in_x, pk_in_y);
  ASSERT_NE(mock_command, nullptr);
  struct ec_response_fp_read_match_secret_with_pubkey response {};
  std::copy(pk_out_x.begin(), pk_out_x.end(), response.pubkey.x);
  std::copy(pk_out_y.begin(), pk_out_y.end(), response.pubkey.y);
  std::copy(iv.begin(), iv.end(), response.iv);
  std::copy(encrypted_secret.begin(), encrypted_secret.end(),
            response.enc_secret);

  EXPECT_CALL(*mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command->EncryptedSecret(), encrypted_secret);
  EXPECT_EQ(mock_command->Iv(), iv);
  EXPECT_EQ(mock_command->PkOutX(), pk_out_x);
  EXPECT_EQ(mock_command->PkOutY(), pk_out_y);
}

}  // namespace
}  // namespace ec
