// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_pairing_key_keygen_command.h"

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::Return;

TEST(FpPairingKeyKeygenCommand, FpPairingKeyKeygen) {
  FpPairingKeyKeygenCommand cmd;
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN);
}

// Mock the underlying EcCommand to test.
class FpPairingKeyKeygenCommandTest : public testing::Test {
 public:
  class MockFpPairingKeyKeygenCommand : public FpPairingKeyKeygenCommand {
   public:
    using FpPairingKeyKeygenCommand::FpPairingKeyKeygenCommand;
    MOCK_METHOD(struct ec_response_fp_establish_pairing_key_keygen*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(FpPairingKeyKeygenCommandTest, Success) {
  const brillo::Blob pub_x(32, 1), pub_y(32, 2);
  const brillo::Blob encrypted_key(sizeof(struct fp_encrypted_private_key), 3);

  MockFpPairingKeyKeygenCommand mock_command;
  struct ec_response_fp_establish_pairing_key_keygen response {};
  std::copy(pub_x.begin(), pub_x.end(), response.pubkey.x);
  std::copy(pub_y.begin(), pub_y.end(), response.pubkey.y);
  std::copy(encrypted_key.begin(), encrypted_key.end(),
            reinterpret_cast<uint8_t*>(&response.encrypted_private_key));

  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command.PubX(), pub_x);
  EXPECT_EQ(mock_command.PubY(), pub_y);
  EXPECT_EQ(mock_command.EncryptedKey(), encrypted_key);
}

}  // namespace
}  // namespace ec
