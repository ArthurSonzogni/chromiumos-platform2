// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_

#include <memory>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"

namespace cryptohome {

// Implementation of the AuthBlockUtility interface to create KeyBlobs with
// AuthBlocks using user credentials and derive KeyBlobs with AuthBlocks using
// credentials and stored AuthBlock state.
class AuthBlockUtilityImpl final : public AuthBlockUtility {
 public:
  // |keyset_management|, |crypto| and |platform| are non-owned objects. Caller
  // must ensure that these objects are valid for the lifetime of
  // AuthBlockUtilityImpl.
  AuthBlockUtilityImpl(KeysetManagement* keyset_management,
                       Crypto* crypto,
                       Platform* platform);
  AuthBlockUtilityImpl(const AuthBlockUtilityImpl&) = delete;
  AuthBlockUtilityImpl& operator=(const AuthBlockUtilityImpl&) = delete;
  ~AuthBlockUtilityImpl() override;

  CryptoError CreateKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const std::optional<brillo::SecureBlob>& reset_secret,
      AuthBlockState& out_state,
      KeyBlobs& out_key_blobs) override;

  CryptoError DeriveKeyBlobsWithAuthBlock(AuthBlockType auth_block_type,
                                          const Credentials& credentials,
                                          const AuthBlockState& state,
                                          KeyBlobs& out_key_blobs) override;

  AuthBlockType GetAuthBlockTypeForCreation(
      const Credentials& credentials) const override;

  AuthBlockType GetAuthBlockTypeForDerivation(
      const Credentials& credentials) const override;

  bool GetAuthBlockStateFromVaultKeyset(const Credentials& credentials,
                                        AuthBlockState& out_state) override;

  void AssignAuthBlockStateToVaultKeyset(const AuthBlockState& state,
                                         VaultKeyset& vault_keyset) override;

  CryptoError CreateKeyBlobsWithAuthFactorType(
      AuthFactorType auth_factor_type,
      const AuthInput& auth_input,
      AuthBlockState& out_auth_block_state,
      KeyBlobs& out_key_blobs) override;

 private:
  // This helper function serves as a factory method to return the authblock
  // used in authentication.
  std::unique_ptr<SyncAuthBlock> GetAuthBlockWithType(
      const AuthBlockType& auth_block_type);

  // Non-owned object used for the keyset management operations. Must be alive
  // for the entire lifecycle of the class.
  KeysetManagement* const keyset_management_;

  // Non-owned crypto object for performing cryptographic operations. Must be
  // alive for the entire lifecycle of the class.
  Crypto* const crypto_;

  // Non-owned platform object used in this class. Must be alive for the entire
  // lifecycle of the class.
  Platform* const platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_
