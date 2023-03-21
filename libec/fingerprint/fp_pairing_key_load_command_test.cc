// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_pairing_key_load_command.h"

#include <algorithm>
#include <cstring>
#include <tuple>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Return;

TEST(FpPairingKeyLoadCommand, IncorrectKeySize) {
  const brillo::Blob encrypted_pairing_key(
      sizeof(struct ec_fp_encrypted_pairing_key) + 1, 1);

  auto cmd = FpPairingKeyLoadCommand::Create(encrypted_pairing_key);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpPairingKeyLoadCommand, FpPairingKeyLoadCommand) {
  const brillo::Blob encrypted_pairing_key(
      sizeof(struct ec_fp_encrypted_pairing_key), 1);

  auto cmd = FpPairingKeyLoadCommand::Create(encrypted_pairing_key);
  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_LOAD_PAIRING_KEY);
  auto pairing_key = std::make_tuple(
      reinterpret_cast<const uint8_t*>(&cmd->Req()->encrypted_pairing_key),
      sizeof(cmd->Req()->encrypted_pairing_key));
  EXPECT_THAT(pairing_key, ElementsAreArray(encrypted_pairing_key));
}

}  // namespace
}  // namespace ec
