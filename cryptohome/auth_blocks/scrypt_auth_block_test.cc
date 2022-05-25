// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/scrypt_auth_block.h"

#include <gtest/gtest.h>

namespace cryptohome {

TEST(ScryptAuthBlockTest, CreateAndDeriveTest) {
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob("foo");

  KeyBlobs create_blobs;

  ScryptAuthBlock auth_block;
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(auth_input, &auth_state, &create_blobs).ok());

  ASSERT_TRUE(create_blobs.vkk_key.has_value());
  EXPECT_FALSE(create_blobs.vkk_key.value().empty());

  KeyBlobs derive_blobs;
  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &derive_blobs).ok());

  EXPECT_EQ(create_blobs.vkk_key, derive_blobs.vkk_key);
}

TEST(ScryptAuthBlockTest, DeriveMissState) {
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob("foo");

  ScryptAuthBlock auth_block;
  KeyBlobs derive_blobs;

  AuthBlockState auth_state;
  EXPECT_FALSE(auth_block.Derive(auth_input, auth_state, &derive_blobs).ok());

  auth_state = AuthBlockState{
      .state =
          ScryptAuthBlockState{
              .work_factor = 16384,
              .block_size = 8,
              .parallel_factor = 1,
          },
  };
  EXPECT_FALSE(auth_block.Derive(auth_input, auth_state, &derive_blobs).ok());

  auth_state = AuthBlockState{
      .state =
          ScryptAuthBlockState{
              .salt = brillo::SecureBlob("salt"),
          },
  };
  EXPECT_FALSE(auth_block.Derive(auth_input, auth_state, &derive_blobs).ok());
}

}  // namespace cryptohome
