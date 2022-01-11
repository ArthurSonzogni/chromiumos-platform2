// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block_state.h"

#include <memory>
#include <optional>

#include <absl/types/variant.h>
#include <brillo/secure_allocator.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_block_state_generated.h"
#include "cryptohome/flatbuffer_secure_allocator_bridge.h"

namespace cryptohome {

namespace {
constexpr int kInitialSize = 4096;

inline flatbuffers::Offset<flatbuffers::Vector<uint8_t>> CreateVector(
    flatbuffers::FlatBufferBuilder* builder, const brillo::SecureBlob& blob) {
  return builder->CreateVector(blob.data(), blob.size());
}

inline bool IsEmpty(const std::optional<brillo::SecureBlob>& blob) {
  return blob.has_value() ? blob.value().empty() : true;
}

// A helper function converts a TpmBoundToPcrAuthBlockState struct into a
// offset.
static flatbuffers::Offset<TpmBoundToPcrState> ToFlatBufferOffset(
    flatbuffers::FlatBufferBuilder* builder,
    const TpmBoundToPcrAuthBlockState* tpm_state) {
  // Converts various SecureBlobs into flatbuffer vectors. Even if a field
  // is optional, build an empty vectors anyway because all vectors
  // should be constructed before the parent table builder initializes.
  auto salt_vector = CreateVector(builder, tpm_state->salt.value());
  auto tpm_key_vector = CreateVector(builder, tpm_state->tpm_key.value());
  auto extended_tpm_key_vector =
      CreateVector(builder, tpm_state->extended_tpm_key.value());
  auto tpm_public_key_hash_vector = CreateVector(
      builder, tpm_state->tpm_public_key_hash.value_or(brillo::SecureBlob()));

  // Construction of the flatbuffer.
  TpmBoundToPcrStateBuilder tpm_buffer_builder(*builder);
  tpm_buffer_builder.add_scrypt_derived(tpm_state->scrypt_derived);
  tpm_buffer_builder.add_salt(salt_vector);
  tpm_buffer_builder.add_tpm_key(tpm_key_vector);
  tpm_buffer_builder.add_extended_tpm_key(extended_tpm_key_vector);
  if (!IsEmpty(tpm_state->tpm_public_key_hash)) {
    tpm_buffer_builder.add_tpm_public_key_hash(tpm_public_key_hash_vector);
  }
  auto tpm_buffer = tpm_buffer_builder.Finish();
  return tpm_buffer;
}

}  // namespace

flatbuffers::Offset<SerializedAuthBlockState> AuthBlockState::SerializeToOffset(
    flatbuffers::FlatBufferBuilder* builder) const {
  flatbuffers::Offset<SerializedAuthBlockState> zero_offset(0);
  if (auto* tpm_state = absl::get_if<TpmBoundToPcrAuthBlockState>(&state)) {
    // Check required fields
    if (IsEmpty(tpm_state->salt)) {
      LOG(ERROR) << "Invalid salt in TpmBoundToPcrAuthBlockState";
      return zero_offset;
    }
    if (IsEmpty(tpm_state->tpm_key)) {
      LOG(ERROR) << "Invalid tpm_key in TpmBoundToPcrAuthBlockState";
      return zero_offset;
    }
    if (IsEmpty(tpm_state->extended_tpm_key)) {
      LOG(ERROR) << "Invalid extended_tpm_key in TpmBoundToPcrAuthBlockState";
      return zero_offset;
    }
    auto tpm_buffer = ToFlatBufferOffset(builder, tpm_state);
    SerializedAuthBlockStateBuilder auth_block_state_builder(*builder);
    auth_block_state_builder.add_auth_block_state_type(
        AuthBlockStateUnion::TpmBoundToPcrState);
    auth_block_state_builder.add_auth_block_state(tpm_buffer.Union());
    auto auth_block_state_offset = auth_block_state_builder.Finish();
    return auth_block_state_offset;
  } else {
    DLOG(ERROR) << "Only TpmBoundtoPcrAuthBlockState can be serialized.";
    return zero_offset;
  }
}

std::optional<brillo::SecureBlob> AuthBlockState::Serialize() const {
  FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(/*initial_size=*/kInitialSize,
                                         &allocator);

  auto auth_block_state_buffer = SerializeToOffset(&builder);
  if (auth_block_state_buffer.IsNull()) {
    DLOG(ERROR) << "AuthBlockState cannot be serialized to offset.";
    return std::nullopt;
  }
  builder.Finish(auth_block_state_buffer);
  uint8_t* buf = builder.GetBufferPointer();
  int size = builder.GetSize();
  brillo::SecureBlob serialized(buf, buf + size);
  return serialized;
}

}  // namespace cryptohome
