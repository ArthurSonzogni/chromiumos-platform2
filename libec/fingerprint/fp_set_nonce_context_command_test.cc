// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_set_nonce_context_command.h"

#include <algorithm>
#include <cstring>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::ElementsAreArray;

TEST(FpSetNonceContextCommand, IncorrectNonceSize) {
  const brillo::Blob nonce(31, 1);
  const brillo::Blob encrypted_user_id(32, 2);
  const brillo::Blob iv(16, 3);

  auto cmd = FpSetNonceContextCommand::Create(nonce, encrypted_user_id, iv);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpSetNonceContextCommand, IncorrectUserIdSize) {
  const brillo::Blob nonce(32, 1);
  const brillo::Blob encrypted_user_id(31, 2);
  const brillo::Blob iv(16, 3);

  auto cmd = FpSetNonceContextCommand::Create(nonce, encrypted_user_id, iv);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpSetNonceContextCommand, IncorrectIvSize) {
  const brillo::Blob nonce(32, 1);
  const brillo::Blob encrypted_user_id(32, 2);
  const brillo::Blob iv(15, 3);

  auto cmd = FpSetNonceContextCommand::Create(nonce, encrypted_user_id, iv);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpSetNonceContextCommand, FpSetNonceContextCommand) {
  const brillo::Blob nonce(32, 1);
  const brillo::Blob encrypted_user_id(32, 2);
  const brillo::Blob iv(16, 3);

  auto cmd = FpSetNonceContextCommand::Create(nonce, encrypted_user_id, iv);
  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_NONCE_CONTEXT);
  EXPECT_THAT(cmd->Req()->gsc_nonce, ElementsAreArray(nonce));
  EXPECT_THAT(cmd->Req()->enc_user_id, ElementsAreArray(encrypted_user_id));
  EXPECT_THAT(cmd->Req()->enc_user_id_iv, ElementsAreArray(iv));
}

}  // namespace
}  // namespace ec
