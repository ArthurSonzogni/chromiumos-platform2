// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block_state_converter.h"

#include <memory>
#include <utility>

#include <absl/types/variant.h>
#include <base/optional.h>
#include <brillo/secure_allocator.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_block_state_generated.h"
#include "cryptohome/flatbuffer_secure_allocator_bridge.h"

namespace cryptohome {

namespace {
constexpr int kInitialSize = 4096;

inline bool IsEmpty(const std::optional<brillo::SecureBlob>& blob) {
  return blob.has_value() ? blob.value().empty() : true;
}

inline flatbuffers::Offset<flatbuffers::Vector<uint8_t>> ToFlatBufferObj(
    flatbuffers::FlatBufferBuilder* builder,
    const std::optional<brillo::SecureBlob>& blob) {
  static const flatbuffers::Offset<flatbuffers::Vector<uint8_t>> kZeroOffset(0);
  return !IsEmpty(blob)
             ? builder->CreateVector(blob.value().data(), blob.value().size())
             : kZeroOffset;
}

inline brillo::SecureBlob ToSecureBlob(
    const flatbuffers::Vector<uint8_t>* vector) {
  return brillo::SecureBlob(vector->data(), vector->data() + vector->size());
}

// A helper function converts a TpmBoundToPcrAuthBlockState struct into a
// offset.
flatbuffers::Offset<TpmBoundToPcrState> ToFlatBufferOffset(
    flatbuffers::FlatBufferBuilder* builder,
    const TpmBoundToPcrAuthBlockState* tpm_state) {
  // Converts various SecureBlobs into flatbuffer vectors. Even if a field
  // is optional, build an empty vectors anyway because all vectors
  // should be constructed before the parent table builder initializes.
  std::optional<bool> scrypt_derived = tpm_state->scrypt_derived;
  auto salt = ToFlatBufferObj(builder, tpm_state->salt);
  auto tpm_key = ToFlatBufferObj(builder, tpm_state->tpm_key);
  auto extended_tpm_key = ToFlatBufferObj(builder, tpm_state->extended_tpm_key);
  auto tpm_public_key_hash =
      ToFlatBufferObj(builder, tpm_state->tpm_public_key_hash);

  // Construction of the flatbuffer.
  TpmBoundToPcrStateBuilder tpm_buffer_builder(*builder);
  if (scrypt_derived.has_value()) {
    tpm_buffer_builder.add_scrypt_derived(scrypt_derived.value());
  }
  tpm_buffer_builder.add_salt(salt);
  tpm_buffer_builder.add_tpm_key(tpm_key);
  tpm_buffer_builder.add_extended_tpm_key(extended_tpm_key);
  tpm_buffer_builder.add_tpm_public_key_hash(tpm_public_key_hash);
  return tpm_buffer_builder.Finish();
}

// A helper function converts a TpmNotBoundToPcrAuthBlockState struct into a
// offset.
flatbuffers::Offset<TpmNotBoundToPcrState> ToFlatBufferOffset(
    flatbuffers::FlatBufferBuilder* builder,
    const TpmNotBoundToPcrAuthBlockState* tpm_state) {
  // Converts various SecureBlobs into flatbuffer vectors. Even if a field
  // is optional, build an empty vectors anyway because all vectors
  // should be constructed before the parent table builder initializes.
  std::optional<bool> scrypt_derived = tpm_state->scrypt_derived;
  std::optional<uint32_t> password_rounds = tpm_state->password_rounds;
  auto salt = ToFlatBufferObj(builder, tpm_state->salt);
  auto tpm_key = ToFlatBufferObj(builder, tpm_state->tpm_key);
  auto tpm_public_key_hash =
      ToFlatBufferObj(builder, tpm_state->tpm_public_key_hash);

  // Construction of the flatbuffer.
  TpmNotBoundToPcrStateBuilder tpm_buffer_builder(*builder);
  if (scrypt_derived.has_value()) {
    tpm_buffer_builder.add_scrypt_derived(scrypt_derived.value());
  }
  tpm_buffer_builder.add_salt(salt);
  tpm_buffer_builder.add_tpm_key(tpm_key);
  tpm_buffer_builder.add_tpm_public_key_hash(tpm_public_key_hash);
  if (password_rounds.has_value()) {
    tpm_buffer_builder.add_password_rounds(password_rounds.value());
  }
  return tpm_buffer_builder.Finish();
}

// A helper function builds a SerializedAuthBlockState from
// a specific AuthBlockState flatbuffer of type T, with the supplied
// Flatbuffers union type.
inline flatbuffers::Offset<SerializedAuthBlockState> FinalizeAuthBlockState(
    flatbuffers::FlatBufferBuilder* builder,
    const flatbuffers::Offset<void>& state,
    const AuthBlockStateUnion& enum_type) {
  SerializedAuthBlockStateBuilder auth_block_state_builder(*builder);
  auth_block_state_builder.add_auth_block_state_type(enum_type);
  auth_block_state_builder.add_auth_block_state(state);
  auto auth_block_state_offset = auth_block_state_builder.Finish();
  return auth_block_state_offset;
}

// A helper checks required fields for TpmBoundToPcrAuthBlockState
bool IsValidAuthBlockState(const TpmBoundToPcrAuthBlockState* tpm_state) {
  if (IsEmpty(tpm_state->salt)) {
    LOG(ERROR) << "Invalid salt in TpmBoundToPcrAuthBlockState";
    return false;
  }
  if (IsEmpty(tpm_state->tpm_key)) {
    LOG(ERROR) << "Invalid tpm_key in TpmBoundToPcrAuthBlockState";
    return false;
  }
  if (IsEmpty(tpm_state->extended_tpm_key)) {
    LOG(ERROR) << "Invalid extended_tpm_key in TpmBoundToPcrAuthBlockState";
    return false;
  }
  return true;
}

// A helper checks required fields for TpmNotBoundToPcrAuthBlockState
bool IsValidAuthBlockState(const TpmNotBoundToPcrAuthBlockState* tpm_state) {
  if (IsEmpty(tpm_state->salt)) {
    LOG(ERROR) << "Invalid salt in TpmNotBoundToPcrAuthBlockState";
    return false;
  }
  if (IsEmpty(tpm_state->tpm_key)) {
    LOG(ERROR) << "Invalid tpm_key in TpmNotBoundToPcrAuthBlockState";
    return false;
  }
  return true;
}
}  // namespace

flatbuffers::Offset<SerializedAuthBlockState> SerializeToFlatBufferOffset(
    flatbuffers::FlatBufferBuilder* builder, const AuthBlockState& state) {
  static const flatbuffers::Offset<SerializedAuthBlockState> kZeroOffset(0);
  if (auto* tpm_state =
          absl::get_if<TpmBoundToPcrAuthBlockState>(&state.state)) {
    if (!IsValidAuthBlockState(tpm_state)) {
      return kZeroOffset;
    }
    auto tpm_buffer = ToFlatBufferOffset(builder, tpm_state);
    return FinalizeAuthBlockState(builder, tpm_buffer.Union(),
                                  AuthBlockStateUnion::TpmBoundToPcrState);
  } else if (auto* tpm_state =
                 absl::get_if<TpmNotBoundToPcrAuthBlockState>(&state.state)) {
    if (!IsValidAuthBlockState(tpm_state)) {
      return kZeroOffset;
    }
    auto tpm_buffer = ToFlatBufferOffset(builder, tpm_state);
    return FinalizeAuthBlockState(builder, tpm_buffer.Union(),
                                  AuthBlockStateUnion::TpmNotBoundToPcrState);
  } else {
    NOTREACHED()
        << "Only TpmBoundtoPcrAuthBlockState/TpmNotBoundToPcrAuthBlockState "
           "can be serialized.";
    return kZeroOffset;
  }
}

std::optional<brillo::SecureBlob> SerializeToFlatBuffer(
    const AuthBlockState& state) {
  FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(/*initial_size=*/kInitialSize,
                                         &allocator);

  auto auth_block_state_buffer = SerializeToFlatBufferOffset(&builder, state);
  if (auth_block_state_buffer.IsNull()) {
    LOG(ERROR) << "AuthBlockState cannot be serialized to offset.";
    return std::nullopt;
  }
  builder.Finish(auth_block_state_buffer);
  uint8_t* buf = builder.GetBufferPointer();
  int size = builder.GetSize();
  brillo::SecureBlob serialized(buf, buf + size);
  return serialized;
}

std::optional<AuthBlockState> DeserializeFromFlatBuffer(
    const brillo::SecureBlob& blob) {
  flatbuffers::Verifier verifier(blob.data(), blob.size());
  if (!VerifySerializedAuthBlockStateBuffer(verifier)) {
    LOG(ERROR) << "Failed verification of a SerializedAuthBlockState buffer.";
    return std::nullopt;
  }
  auto* state_buffer = GetSerializedAuthBlockState(blob.data());
  if (!state_buffer) {
    LOG(ERROR) << "Failed to deserialize blob as SerializedAuthBlockState.";
    return std::nullopt;
  }
  return FromFlatBuffer(*state_buffer);
}

std::optional<AuthBlockState> FromFlatBuffer(
    const SerializedAuthBlockState& state) {
  switch (state.auth_block_state_type()) {
    case AuthBlockStateUnion::TpmBoundToPcrState: {
      auto* tpm_buffer =
          static_cast<const TpmBoundToPcrState*>(state.auth_block_state());
      if (!tpm_buffer->salt()) {
        LOG(ERROR) << "Bad TpmBoundToPcrState: missing salt.";
        return std::nullopt;
      }
      if (!tpm_buffer->scrypt_derived()) {
        LOG(ERROR)
            << "Bad TpmBoundToPcrState: scrypt_derived should not be false.";
        return std::nullopt;
      }
      if (!tpm_buffer->tpm_key()) {
        LOG(ERROR) << "Bad TpmBoundToPcrState: missing tpm_key.";
        return std::nullopt;
      }
      if (!tpm_buffer->extended_tpm_key()) {
        LOG(ERROR) << "Bad TpmBoundToPcrState: missing extended_tpm_key.";
        return std::nullopt;
      }
      TpmBoundToPcrAuthBlockState tpm_state = {};
      tpm_state.scrypt_derived = tpm_buffer->scrypt_derived();
      tpm_state.salt = ToSecureBlob(tpm_buffer->salt());
      tpm_state.tpm_key = ToSecureBlob(tpm_buffer->tpm_key());
      tpm_state.extended_tpm_key = ToSecureBlob(tpm_buffer->extended_tpm_key());
      if (tpm_buffer->tpm_public_key_hash() &&
          (tpm_buffer->tpm_public_key_hash()->size() > 0)) {
        tpm_state.tpm_public_key_hash =
            ToSecureBlob(tpm_buffer->tpm_public_key_hash());
      }
      AuthBlockState state = {.state = std::move(tpm_state)};
      return state;
    }
    case AuthBlockStateUnion::TpmNotBoundToPcrState: {
      auto* tpm_buffer =
          static_cast<const TpmNotBoundToPcrState*>(state.auth_block_state());
      if (!tpm_buffer->salt()) {
        LOG(ERROR) << "Bad TpmNotBoundToPcrState: missing salt.";
        return std::nullopt;
      }
      if (!tpm_buffer->scrypt_derived()) {
        LOG(ERROR)
            << "Bad TpmNotBoundToPcrState: scrypt_derived should not be false.";
        return std::nullopt;
      }
      if (!tpm_buffer->tpm_key()) {
        LOG(ERROR) << "Bad TpmNotBoundToPcrState: missing tpm_key.";
        return std::nullopt;
      }
      TpmNotBoundToPcrAuthBlockState tpm_state = {};
      tpm_state.scrypt_derived = tpm_buffer->scrypt_derived();
      tpm_state.salt = ToSecureBlob(tpm_buffer->salt());
      tpm_state.tpm_key = ToSecureBlob(tpm_buffer->tpm_key());
      if (tpm_buffer->password_rounds() != 0) {
        tpm_state.password_rounds = tpm_buffer->password_rounds();
      }
      if (tpm_buffer->tpm_public_key_hash()) {
        tpm_state.tpm_public_key_hash =
            ToSecureBlob(tpm_buffer->tpm_public_key_hash());
      }
      AuthBlockState state = {.state = std::move(tpm_state)};
      return state;
    }
    default:
      DLOG(ERROR)
          << "Only support "
             "TpmBoundToPcrAuthBlockState/TpmNotBoundToPcrAuthBlockState "
             "deserialization.";
      return std::nullopt;
  }
}
}  // namespace cryptohome
