// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_

#include <memory>
#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_challenge_service.h"
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
  AuthBlockUtilityImpl(
      KeysetManagement* keyset_management,
      Crypto* crypto,
      Platform* platform,
      ChallengeCredentialsHelper* credentials_helper,
      std::unique_ptr<KeyChallengeService> key_challenge_service,
      const std::string& account_id);
  AuthBlockUtilityImpl(const AuthBlockUtilityImpl&) = delete;
  AuthBlockUtilityImpl& operator=(const AuthBlockUtilityImpl&) = delete;
  ~AuthBlockUtilityImpl() override;

  bool GetLockedToSingleUser() override;

  CryptoError CreateKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const std::optional<brillo::SecureBlob>& reset_secret,
      AuthBlockState& out_state,
      KeyBlobs& out_key_blobs) override;

  // Creates KeyBlobs and AuthBlockState for the given credentials and returns
  // through the asynchronous create_callback.
  bool CreateKeyBlobsWithAuthBlockAsync(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const std::optional<brillo::SecureBlob>& reset_secret,
      AuthBlock::CreateCallback create_callback) override;

  CryptoError DeriveKeyBlobsWithAuthBlock(AuthBlockType auth_block_type,
                                          const Credentials& credentials,
                                          const AuthBlockState& state,
                                          KeyBlobs& out_key_blobs) override;

  // Creates KeyBlobs and AuthBlockState for the given credentials and returns
  // through the asynchronous derive_callback.
  bool DeriveKeyBlobsWithAuthBlockAsync(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const AuthBlockState& auth_state,
      AuthBlock::DeriveCallback derive_callback) override;

  AuthBlockType GetAuthBlockTypeForCreation(
      const bool is_le_credential,
      const bool is_challenge_credential) const override;

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
  CryptoError DeriveKeyBlobs(const AuthInput& auth_input,
                             const AuthBlockState& auth_block_state,
                             KeyBlobs& out_key_blobs) override;

 private:
  // This helper function serves as a factory method to return the authblock
  // used in authentication.
  std::unique_ptr<SyncAuthBlock> GetAuthBlockWithType(
      const AuthBlockType& auth_block_type);

  // This helper function returns an authblock with asynchronous create and
  // derive.
  std::unique_ptr<AuthBlock> GetAsyncAuthBlockWithType(
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

  // Challenge credential helper utility object. This object is required
  // for using a challenge response authblock.
  ChallengeCredentialsHelper* const challenge_credentials_helper_;

  // KeyChallengeService is tasked with contacting the challenge response D-Bus
  // service that'll provide the response once we send the challenge.
  std::unique_ptr<KeyChallengeService> key_challenge_service_;

  // Account ID for AsyncChallengeCredentialAuthBlock.
  std::optional<std::string> account_id_;

  friend class AuthBlockUtilityImplTest;
  FRIEND_TEST(AuthBlockUtilityImplTest, GetAsyncAuthBlockWithType);
  FRIEND_TEST(AuthBlockUtilityImplTest, GetAsyncAuthBlockWithTypeFail);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_
