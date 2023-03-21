// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_pairing_key_wrap_command.h"

#include <algorithm>
#include <cstring>
#include <tuple>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Return;

TEST(FpPairingKeyWrapCommand, IncorrectXSize) {
  const brillo::Blob pub_x(33, 1);
  const brillo::Blob pub_y(32, 2);
  const brillo::Blob encrypted_priv(sizeof(struct fp_encrypted_private_key), 3);

  auto cmd = FpPairingKeyWrapCommand::Create(pub_x, pub_y, encrypted_priv);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpPairingKeyWrapCommand, IncorrectYSize) {
  const brillo::Blob pub_x(32, 1);
  const brillo::Blob pub_y(33, 2);
  const brillo::Blob encrypted_priv(sizeof(struct fp_encrypted_private_key), 3);

  auto cmd = FpPairingKeyWrapCommand::Create(pub_x, pub_y, encrypted_priv);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpPairingKeyWrapCommand, IncorrectKeySize) {
  const brillo::Blob pub_x(32, 1);
  const brillo::Blob pub_y(32, 2);
  const brillo::Blob encrypted_priv(sizeof(struct fp_encrypted_private_key) + 1,
                                    3);

  auto cmd = FpPairingKeyWrapCommand::Create(pub_x, pub_y, encrypted_priv);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpPairingKeyWrapCommand, FpPairingKeyWrapCommand) {
  const brillo::Blob pub_x(32, 1);
  const brillo::Blob pub_y(32, 2);
  const brillo::Blob encrypted_priv(sizeof(struct fp_encrypted_private_key), 3);

  auto cmd = FpPairingKeyWrapCommand::Create(pub_x, pub_y, encrypted_priv);
  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP);
  EXPECT_THAT(cmd->Req()->peers_pubkey.x, ElementsAreArray(pub_x));
  EXPECT_THAT(std::make_tuple(reinterpret_cast<const uint8_t*>(
                                  &cmd->Req()->encrypted_private_key),
                              sizeof(struct fp_encrypted_private_key)),
              ElementsAreArray(encrypted_priv));
  EXPECT_THAT(cmd->Req()->peers_pubkey.x, ElementsAreArray(pub_x));
  EXPECT_THAT(cmd->Req()->peers_pubkey.y, ElementsAreArray(pub_y));
}

// Mock the underlying EcCommand to test.
class FpPairingKeyWrapCommandTest : public testing::Test {
 public:
  class MockFpPairingKeyWrapCommand : public FpPairingKeyWrapCommand {
   public:
    using FpPairingKeyWrapCommand::FpPairingKeyWrapCommand;
    MOCK_METHOD(struct ec_response_fp_establish_pairing_key_wrap*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(FpPairingKeyWrapCommandTest, Success) {
  const brillo::Blob pub_x(32, 1);
  const brillo::Blob pub_y(32, 2);
  const brillo::Blob encrypted_priv(sizeof(struct fp_encrypted_private_key), 3);
  const brillo::Blob encrypted_pairing_key(
      sizeof(struct ec_fp_encrypted_pairing_key), 4);

  auto mock_command =
      MockFpPairingKeyWrapCommand::Create<MockFpPairingKeyWrapCommand>(
          pub_x, pub_y, encrypted_priv);
  ASSERT_NE(mock_command, nullptr);
  struct ec_response_fp_establish_pairing_key_wrap response {};
  std::copy(encrypted_pairing_key.begin(), encrypted_pairing_key.end(),
            reinterpret_cast<uint8_t*>(&response.encrypted_pairing_key));

  EXPECT_CALL(*mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command->EncryptedPairingKey(), encrypted_pairing_key);
}

}  // namespace
}  // namespace ec
