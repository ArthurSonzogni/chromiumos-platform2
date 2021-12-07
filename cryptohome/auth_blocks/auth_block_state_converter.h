// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_STATE_CONVERTER_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_STATE_CONVERTER_H_

#include <optional>
#include <variant>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_block_state_generated.h"
#include "cryptohome/auth_blocks/auth_block_state.h"

namespace cryptohome {

// Returns an Flatbuffer offset which can be added to other Flatbuffers
// tables. Returns a zero offset for errors since AuthBlockState
// shall never be an empty table. Zero offset can be checked by flatbuffers
// library standard method IsNull().
flatbuffers::Offset<SerializedAuthBlockState> SerializeToFlatBufferOffset(
    flatbuffers::FlatBufferBuilder* builder, const AuthBlockState& state);

// Returns an AuthBlockState Flatbuffer serialized to a SecureBlob.
std::optional<brillo::SecureBlob> SerializeToFlatBuffer(
    const AuthBlockState& state);

// Populates state from a Flatbuffers blob.
std::optional<AuthBlockState> DeserializeFromFlatBuffer(
    const brillo::SecureBlob& blob);

// Converts to AuthBlockState struct from a FlatBuffers object.
std::optional<AuthBlockState> FromFlatBuffer(
    const SerializedAuthBlockState& state);
}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_STATE_CONVERTER_H_
