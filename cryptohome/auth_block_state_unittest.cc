// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <absl/types/variant.h>
#include <base/optional.h>
#include <brillo/secure_allocator.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_block_state.h"
#include "cryptohome/auth_block_state_generated.h"
#include "cryptohome/flatbuffer_secure_allocator_bridge.h"

namespace cryptohome {

namespace {
constexpr char kFakeSalt[] = "fake_salt";
constexpr char kFakeTpmKey[] = "fake_tpm_key";
constexpr char kFakeExtendedTpmKey[] = "fake_extended_tpm_key";
constexpr char kFakeTpmKeyHash[] = "fake_tpm_key_hash";

brillo::SecureBlob ToSecureBlob(const flatbuffers::Vector<uint8_t>* vector) {
  return brillo::SecureBlob(vector->data(), vector->data() + vector->size());
}

}  // namespace

TEST(AuthBlockStateTest, SerializeTpmBoundtoPcrState) {
  const brillo::SecureBlob salt(brillo::BlobFromString(kFakeSalt));
  const brillo::SecureBlob tpm_key(brillo::BlobFromString(kFakeTpmKey));
  const brillo::SecureBlob extended_tpm_key(
      brillo::BlobFromString(kFakeExtendedTpmKey));
  const brillo::SecureBlob tpm_public_key_hash(
      brillo::BlobFromString(kFakeTpmKeyHash));
  TpmBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true,
      .salt = salt,
      .tpm_key = tpm_key,
      .extended_tpm_key = extended_tpm_key,
      .tpm_public_key_hash = tpm_public_key_hash,
  };
  AuthBlockState final_state = {.state = std::move(tpm_state)};

  base::Optional<brillo::SecureBlob> serialized = final_state.Serialize();

  ASSERT_TRUE(serialized.has_value());
  auto* state_buffer = GetSerializedAuthBlockState(serialized->data());
  EXPECT_EQ(state_buffer->auth_block_state_type(),
            AuthBlockStateUnion::TpmBoundToPcrState);
  auto* tpm_buffer =
      static_cast<const TpmBoundToPcrState*>(state_buffer->auth_block_state());
  EXPECT_TRUE(tpm_buffer->scrypt_derived());
  EXPECT_EQ(ToSecureBlob(tpm_buffer->salt()), salt);
  EXPECT_EQ(ToSecureBlob(tpm_buffer->tpm_key()), tpm_key);
  EXPECT_EQ(ToSecureBlob(tpm_buffer->extended_tpm_key()), extended_tpm_key);
  EXPECT_EQ(ToSecureBlob(tpm_buffer->tpm_public_key_hash()),
            tpm_public_key_hash);
}

TEST(AuthBlockStateTest, SerializedAuthBlockStateOffset) {
  FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(/*initial_size=*/1024, &allocator);
  const brillo::SecureBlob salt(brillo::BlobFromString(kFakeSalt));
  const brillo::SecureBlob tpm_key(brillo::BlobFromString(kFakeTpmKey));
  const brillo::SecureBlob extended_tpm_key(
      brillo::BlobFromString(kFakeExtendedTpmKey));
  const brillo::SecureBlob tpm_public_key_hash(
      brillo::BlobFromString(kFakeTpmKeyHash));
  TpmBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true,
      .salt = salt,
      .tpm_key = tpm_key,
      .extended_tpm_key = extended_tpm_key,
      .tpm_public_key_hash = tpm_public_key_hash,
  };
  AuthBlockState final_state = {.state = std::move(tpm_state)};

  flatbuffers::Offset<SerializedAuthBlockState> offset =
      final_state.SerializeToOffset(&builder);

  ASSERT_TRUE(!offset.IsNull());
}

TEST(AuthBlockStateTest, TpmBoundtoPcrStateOptionalFields) {
  const brillo::SecureBlob salt(brillo::BlobFromString(kFakeSalt));
  const brillo::SecureBlob tpm_key(brillo::BlobFromString(kFakeTpmKey));
  const brillo::SecureBlob extended_tpm_key(
      brillo::BlobFromString(kFakeExtendedTpmKey));
  TpmBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true,
      .salt = salt,
      .tpm_key = tpm_key,
      .extended_tpm_key = extended_tpm_key,
      .tpm_public_key_hash = brillo::SecureBlob(),
  };
  AuthBlockState final_state = {.state = std::move(tpm_state)};

  base::Optional<brillo::SecureBlob> serialized = final_state.Serialize();

  ASSERT_TRUE(serialized.has_value());
  auto* state_buffer = GetSerializedAuthBlockState(serialized->data());
  EXPECT_EQ(state_buffer->auth_block_state_type(),
            AuthBlockStateUnion::TpmBoundToPcrState);
  auto* tpm_buffer =
      static_cast<const TpmBoundToPcrState*>(state_buffer->auth_block_state());
  EXPECT_TRUE(tpm_buffer->scrypt_derived());
  EXPECT_EQ(ToSecureBlob(tpm_buffer->salt()), salt);
  EXPECT_EQ(ToSecureBlob(tpm_buffer->tpm_key()), tpm_key);
  EXPECT_EQ(ToSecureBlob(tpm_buffer->extended_tpm_key()), extended_tpm_key);
  EXPECT_EQ(tpm_buffer->tpm_public_key_hash(), nullptr);
}

TEST(AuthBlockStateTest, TpmBoundtoPcrStateFail) {
  TpmBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true,
  };
  AuthBlockState final_state = {.state = std::move(tpm_state)};

  base::Optional<brillo::SecureBlob> serialized = final_state.Serialize();

  // tpm_key, extended_tpm_key and salt are all missing, cannot serialize()
  EXPECT_EQ(serialized, base::nullopt);
}

}  // namespace cryptohome
