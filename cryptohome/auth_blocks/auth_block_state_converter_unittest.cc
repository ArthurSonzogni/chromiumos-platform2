// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <absl/types/variant.h>
#include <base/optional.h>
#include <brillo/secure_allocator.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_block_state_generated.h"
#include "cryptohome/auth_blocks/auth_block_state_converter.h"
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

inline flatbuffers::Offset<flatbuffers::Vector<uint8_t>> CreateVector(
    flatbuffers::FlatBufferBuilder* builder, const brillo::SecureBlob& blob) {
  return builder->CreateVector(blob.data(), blob.size());
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

  base::Optional<brillo::SecureBlob> serialized =
      SerializeToFlatBuffer(final_state);

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
  flatbuffers::FlatBufferBuilder builder;
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
      SerializeToFlatBufferOffset(&builder, final_state);

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

  base::Optional<brillo::SecureBlob> serialized =
      SerializeToFlatBuffer(final_state);

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

  base::Optional<brillo::SecureBlob> serialized =
      SerializeToFlatBuffer(final_state);

  // tpm_key, extended_tpm_key and salt are all missing, cannot serialize()
  EXPECT_EQ(serialized, base::nullopt);
}

TEST(AuthBlockStateTest, TpmNotBoundtoPcrStateOptionalFields) {
  const brillo::SecureBlob salt(brillo::BlobFromString(kFakeSalt));
  const brillo::SecureBlob tpm_key(brillo::BlobFromString(kFakeTpmKey));
  TpmNotBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true,
      .salt = salt,
      .tpm_key = tpm_key,
  };
  AuthBlockState final_state = {.state = std::move(tpm_state)};

  base::Optional<brillo::SecureBlob> serialized =
      SerializeToFlatBuffer(final_state);

  ASSERT_TRUE(serialized.has_value());
  auto* state_buffer = GetSerializedAuthBlockState(serialized->data());
  EXPECT_EQ(state_buffer->auth_block_state_type(),
            AuthBlockStateUnion::TpmNotBoundToPcrState);
  auto* tpm_buffer = static_cast<const TpmNotBoundToPcrState*>(
      state_buffer->auth_block_state());
  EXPECT_TRUE(tpm_buffer->scrypt_derived());
  EXPECT_EQ(ToSecureBlob(tpm_buffer->salt()), salt);
  EXPECT_EQ(ToSecureBlob(tpm_buffer->tpm_key()), tpm_key);
  EXPECT_EQ(tpm_buffer->tpm_public_key_hash(), nullptr);
  EXPECT_EQ(tpm_buffer->password_rounds(), base::nullopt);
}

TEST(AuthBlockStateTest, TpmNotBoundtoPcrStateFail) {
  TpmBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true,
  };
  AuthBlockState final_state = {.state = std::move(tpm_state)};

  base::Optional<brillo::SecureBlob> serialized =
      SerializeToFlatBuffer(final_state);

  // tpm_key, salt are both missing, cannot serialize()
  EXPECT_EQ(serialized, base::nullopt);
}

TEST(AuthBlockStateTest, TpmBoundToPcrStateDeserialization) {
  flatbuffers::FlatBufferBuilder builder;
  brillo::SecureBlob salt_blob(brillo::BlobFromString(kFakeSalt));
  brillo::SecureBlob tpm_key_blob(brillo::BlobFromString(kFakeTpmKey));
  brillo::SecureBlob extended_tpm_key_blob(
      brillo::BlobFromString(kFakeExtendedTpmKey));
  brillo::SecureBlob tpm_public_key_hash_blob(
      brillo::BlobFromString(kFakeTpmKeyHash));
  auto salt_vector = CreateVector(&builder, salt_blob);
  auto tpm_key_vector = CreateVector(&builder, tpm_key_blob);
  auto extended_tpm_key_vector = CreateVector(&builder, extended_tpm_key_blob);
  auto tpm_public_key_hash_vector =
      CreateVector(&builder, tpm_public_key_hash_blob);
  // Construction of the flatbuffer.
  TpmBoundToPcrStateBuilder tpm_buffer_builder(builder);
  tpm_buffer_builder.add_scrypt_derived(true);
  tpm_buffer_builder.add_salt(salt_vector);
  tpm_buffer_builder.add_tpm_key(tpm_key_vector);
  tpm_buffer_builder.add_extended_tpm_key(extended_tpm_key_vector);
  tpm_buffer_builder.add_tpm_public_key_hash(tpm_public_key_hash_vector);
  auto tpm_buffer = tpm_buffer_builder.Finish();
  SerializedAuthBlockStateBuilder auth_block_state_builder(builder);
  auth_block_state_builder.add_auth_block_state_type(
      AuthBlockStateUnion::TpmBoundToPcrState);
  auth_block_state_builder.add_auth_block_state(tpm_buffer.Union());
  auto auth_block_state_buffer = auth_block_state_builder.Finish();
  builder.Finish(auth_block_state_buffer);
  uint8_t* buf = builder.GetBufferPointer();
  int size = builder.GetSize();
  brillo::SecureBlob serialized(buf, buf + size);

  base::Optional<AuthBlockState> state = DeserializeFromFlatBuffer(serialized);
  ASSERT_TRUE(state.has_value());

  auto* tpm_state = absl::get_if<TpmBoundToPcrAuthBlockState>(&state->state);
  EXPECT_TRUE(tpm_state->scrypt_derived);
  EXPECT_EQ(tpm_state->salt.value(), salt_blob);
  EXPECT_EQ(tpm_state->tpm_key.value(), tpm_key_blob);
  EXPECT_EQ(tpm_state->extended_tpm_key.value(), extended_tpm_key_blob);
  EXPECT_EQ(tpm_state->tpm_public_key_hash.value(), tpm_public_key_hash_blob);
}

TEST(AuthBlockStateTest, TpmNotBoundToPcrStateDeserialization) {
  flatbuffers::FlatBufferBuilder builder;
  brillo::SecureBlob salt_blob(brillo::BlobFromString(kFakeSalt));
  brillo::SecureBlob tpm_key_blob(brillo::BlobFromString(kFakeTpmKey));
  brillo::SecureBlob tpm_public_key_hash_blob(
      brillo::BlobFromString(kFakeTpmKeyHash));
  auto salt_vector = CreateVector(&builder, salt_blob);
  auto tpm_key_vector = CreateVector(&builder, tpm_key_blob);
  auto tpm_public_key_hash_vector =
      CreateVector(&builder, tpm_public_key_hash_blob);
  // Construction of the flatbuffer.
  TpmNotBoundToPcrStateBuilder tpm_buffer_builder(builder);
  tpm_buffer_builder.add_scrypt_derived(true);
  tpm_buffer_builder.add_salt(salt_vector);
  tpm_buffer_builder.add_password_rounds(32);
  tpm_buffer_builder.add_tpm_key(tpm_key_vector);
  tpm_buffer_builder.add_tpm_public_key_hash(tpm_public_key_hash_vector);
  auto tpm_buffer = tpm_buffer_builder.Finish();
  SerializedAuthBlockStateBuilder auth_block_state_builder(builder);
  auth_block_state_builder.add_auth_block_state_type(
      AuthBlockStateUnion::TpmNotBoundToPcrState);
  auth_block_state_builder.add_auth_block_state(tpm_buffer.Union());
  auto auth_block_state_buffer = auth_block_state_builder.Finish();
  builder.Finish(auth_block_state_buffer);
  uint8_t* buf = builder.GetBufferPointer();
  int size = builder.GetSize();
  brillo::SecureBlob serialized(buf, buf + size);

  base::Optional<AuthBlockState> state = DeserializeFromFlatBuffer(serialized);
  ASSERT_TRUE(state.has_value());

  auto* tpm_state = absl::get_if<TpmNotBoundToPcrAuthBlockState>(&state->state);
  EXPECT_TRUE(tpm_state->scrypt_derived);
  EXPECT_EQ(tpm_state->password_rounds.value(), 32);
  EXPECT_EQ(tpm_state->salt.value(), salt_blob);
  EXPECT_EQ(tpm_state->tpm_key.value(), tpm_key_blob);
  EXPECT_EQ(tpm_state->tpm_public_key_hash.value(), tpm_public_key_hash_blob);
}
}  // namespace cryptohome
