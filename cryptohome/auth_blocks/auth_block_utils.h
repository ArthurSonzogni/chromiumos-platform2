// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILS_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILS_H_

#include <stdint.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

struct AuthBlockFlags {
  int32_t require_flags;
  int32_t refuse_flags;
  AuthBlockType auth_block_type;
};

constexpr AuthBlockFlags kPinWeaverFlags = {
    .require_flags = SerializedVaultKeyset::LE_CREDENTIAL,
    .refuse_flags = 0,
    .auth_block_type = AuthBlockType::kPinWeaver,
};

constexpr AuthBlockFlags kChallengeCredentialFlags = {
    .require_flags = SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED,
    .refuse_flags = 0,
    .auth_block_type = AuthBlockType::kChallengeCredential,
};

constexpr AuthBlockFlags kDoubleWrappedCompatFlags = {
    .require_flags = SerializedVaultKeyset::SCRYPT_WRAPPED |
                     SerializedVaultKeyset::TPM_WRAPPED,
    .refuse_flags = 0,
    .auth_block_type = AuthBlockType::kDoubleWrappedCompat,
};

constexpr AuthBlockFlags kLibScryptCompatFlags = {
    .require_flags = SerializedVaultKeyset::SCRYPT_WRAPPED,
    .refuse_flags = SerializedVaultKeyset::TPM_WRAPPED |
                    SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED,
    .auth_block_type = AuthBlockType::kLibScryptCompat,
};

constexpr AuthBlockFlags kTpmNotBoundToPcrFlags = {
    .require_flags = SerializedVaultKeyset::TPM_WRAPPED,
    .refuse_flags = SerializedVaultKeyset::SCRYPT_WRAPPED |
                    SerializedVaultKeyset::PCR_BOUND |
                    SerializedVaultKeyset::ECC,
    .auth_block_type = AuthBlockType::kTpmNotBoundToPcr,
};

constexpr AuthBlockFlags kTpmBoundToPcrFlags = {
    .require_flags =
        SerializedVaultKeyset::TPM_WRAPPED | SerializedVaultKeyset::PCR_BOUND,
    .refuse_flags =
        SerializedVaultKeyset::SCRYPT_WRAPPED | SerializedVaultKeyset::ECC,
    .auth_block_type = AuthBlockType::kTpmBoundToPcr,
};

constexpr AuthBlockFlags kTpmEccFlags = {
    .require_flags = SerializedVaultKeyset::TPM_WRAPPED |
                     SerializedVaultKeyset::SCRYPT_DERIVED |
                     SerializedVaultKeyset::PCR_BOUND |
                     SerializedVaultKeyset::ECC,
    .refuse_flags = SerializedVaultKeyset::SCRYPT_WRAPPED,
    .auth_block_type = AuthBlockType::kTpmEcc,
};

// Coverts the AuthBlock flags defined by an integer value to AuthBlockType.
bool FlagsToAuthBlockType(int32_t flags, AuthBlockType& out_auth_block_type);

// Obtains the AuthBlockState stored in a VaultKeyset.
bool GetAuthBlockState(const VaultKeyset& vk, AuthBlockState& out_state);

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILS_H_
