// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_

#include <base/compiler_specific.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

// This is a utility class to create KeyBlobs with AuthBlocks using user
// credentials and derive KeyBlobs with AuthBlocks using credentials and stored
// AuthBlock state.
class AuthBlockUtility {
 public:
  AuthBlockUtility() = default;
  AuthBlockUtility(const AuthBlockUtility&) = delete;
  AuthBlockUtility& operator=(const AuthBlockUtility&) = delete;
  virtual ~AuthBlockUtility() = default;

  // Creates KeyBlobs and AuthBlockState with the given type of AuthBlock for
  // the given credentials. Creating KeyBlobs means generating the KeyBlobs from
  // user credentials when the credentials are entered first time. Thus,
  // CreateKeyBlobsWithAuthBlock() should be called to generate the KeyBlobs for
  // adding initial key, adding key and migrating a key.
  virtual CryptoError CreateKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const std::optional<brillo::SecureBlob>& reset_secret,
      AuthBlockState& out_state,
      KeyBlobs& out_key_blobs) = 0;

  // Derives KeyBlobs with the given type of AuthBlock using the passed
  // credentials and the AuthBlockState. Deriving KeyBlobs means generating the
  // KeyBlobs from entered credentials and the stored metadata for an existing
  // key. Thus DeriveKeyBlobsWithAuthBlock() should be called to generate the
  // KeyBlob for loading an existing wrapped key from the disk for user
  // authentication.
  virtual CryptoError DeriveKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const AuthBlockState& state,
      KeyBlobs& out_key_blobs) = 0;

  // This function returns the AuthBlock type for
  // AuthBlock::Create() based on the |credentials|, |tpm_| and |crypto_|
  // status.
  virtual AuthBlockType GetAuthBlockTypeForCreation(
      const Credentials& credentials) const = 0;

  // This function returns the AuthBlock type for AuthBlock::Derive()
  // based on the vault keyset flags value.
  virtual AuthBlockType GetAuthBlockTypeForDerivation(
      const Credentials& credentials) const = 0;

  // This populates an AuthBlockState allocated by the caller.
  virtual bool GetAuthBlockStateFromVaultKeyset(const Credentials& credentials,
                                                AuthBlockState& out_state) = 0;

  // Reads an auth block state and update the given VaultKeyset with what it
  // returns.
  virtual void AssignAuthBlockStateToVaultKeyset(const AuthBlockState& state,
                                                 VaultKeyset& vault_keyset) = 0;

  // Creates a new auth block state and key blobs using an auth block. On error,
  // returns the error code.
  virtual CryptoError CreateKeyBlobsWithAuthFactorType(
      AuthFactorType auth_factor_type,
      const AuthInput& auth_input,
      AuthBlockState& out_auth_block_state,
      KeyBlobs& out_key_blobs) WARN_UNUSED_RESULT = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_
