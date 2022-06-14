// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_

#include <memory>
#include <optional>
#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

enum class AuthFactorStorageType {
  kVaultKeyset,
  kUserSecretStash,
};

// This is a utility class to create KeyBlobs with AuthBlocks using user
// credentials and derive KeyBlobs with AuthBlocks using credentials and stored
// AuthBlock state.
class AuthBlockUtility {
 public:
  AuthBlockUtility() = default;
  AuthBlockUtility(const AuthBlockUtility&) = delete;
  AuthBlockUtility& operator=(const AuthBlockUtility&) = delete;
  virtual ~AuthBlockUtility() = default;

  // Returns whether the system is locked to only allow authenticating a single
  // user.
  virtual bool GetLockedToSingleUser() const = 0;

  // Creates KeyBlobs and AuthBlockState with the given type of AuthBlock for
  // the given credentials. Creating KeyBlobs means generating the KeyBlobs from
  // user credentials when the credentials are entered first time. Thus,
  // CreateKeyBlobsWithAuthBlock() should be called to generate the KeyBlobs for
  // adding initial key, adding key and migrating a key.
  virtual CryptoStatus CreateKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const std::optional<brillo::SecureBlob>& reset_secret,
      AuthBlockState& out_state,
      KeyBlobs& out_key_blobs) const = 0;

  // Creates KeyBlobs and AuthBlockState for the given credentials and returns
  // through the asynchronous create_callback.
  virtual bool CreateKeyBlobsWithAuthBlockAsync(
      AuthBlockType auth_block_type,
      const AuthInput& auth_input,
      AuthBlock::CreateCallback create_callback) = 0;

  // Derives KeyBlobs with the given type of AuthBlock using the passed
  // credentials and the AuthBlockState. Deriving KeyBlobs means generating the
  // KeyBlobs from entered credentials and the stored metadata for an existing
  // key. Thus DeriveKeyBlobsWithAuthBlock() should be called to generate the
  // KeyBlob for loading an existing wrapped key from the disk for user
  // authentication.
  virtual CryptoStatus DeriveKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const AuthBlockState& state,
      KeyBlobs& out_key_blobs) const = 0;

  // Creates KeyBlobs and AuthBlockState for the given credentials and returns
  // through the asynchronous derive_callback.
  virtual bool DeriveKeyBlobsWithAuthBlockAsync(
      AuthBlockType auth_block_type,
      const AuthInput& auth_input,
      const AuthBlockState& auth_state,
      AuthBlock::DeriveCallback derive_callback) = 0;

  // This function returns the AuthBlock type for
  // AuthBlock::Create() based on the |credentials|, |tpm_| and |crypto_|
  // status.
  virtual AuthBlockType GetAuthBlockTypeForCreation(
      const bool is_le_credential,
      const bool is_recovery,
      const bool is_challenge_credential,
      const AuthFactorStorageType auth_factor_storage_type) const = 0;

  // This function returns the AuthBlock type for
  // AuthBlock::Derive() based on AutBlockState.
  virtual AuthBlockType GetAuthBlockTypeForDerive(
      const AuthBlockState& state) const = 0;

  // This function returns the AuthBlock type for AuthBlock::Derive()
  // based on the vault keyset flags value.
  virtual AuthBlockType GetAuthBlockTypeForDerivation(
      const std::string& label,
      const std::string& obfuscated_username) const = 0;

  // This populates an AuthBlockState allocated by the caller.
  virtual bool GetAuthBlockStateFromVaultKeyset(
      const std::string& label,
      const std::string& obfuscated_username,
      AuthBlockState& out_state) const = 0;

  // Reads an auth block state and update the given VaultKeyset with what it
  // returns.
  virtual void AssignAuthBlockStateToVaultKeyset(
      const AuthBlockState& state, VaultKeyset& vault_keyset) const = 0;

  // Creates a new auth block state and key blobs using an auth block. On error,
  // returns the error code.
  [[nodiscard]] virtual CryptoStatus CreateKeyBlobsWithAuthFactorType(
      AuthFactorType auth_factor_type,
      const AuthFactorStorageType auth_factor_storage_type,
      const AuthInput& auth_input,
      AuthBlockState& out_auth_block_state,
      KeyBlobs& out_key_blobs) const = 0;

  // Derives key blobs using the given auth block state and input.
  [[nodiscard]] virtual CryptoStatus DeriveKeyBlobs(
      const AuthInput& auth_input,
      const AuthBlockState& auth_block_state,
      KeyBlobs& out_key_blobs) const = 0;

  // Generates a payload for cryptohome recovery AuthFactor authentication.
  [[nodiscard]] virtual CryptoStatus GenerateRecoveryRequest(
      const std::string& obfuscated_username,
      const cryptorecovery::RequestMetadata& request_metadata,
      const brillo::Blob& epoch_response,
      const CryptohomeRecoveryAuthBlockState& state,
      Tpm* tpm,
      brillo::SecureBlob* out_recovery_request,
      brillo::SecureBlob* out_ephemeral_pub_key) const = 0;

  // Provides a KeyChallengeService for ChallengeCredentials to
  // either create or derive KeyBlobs.
  virtual void SetSingleUseKeyChallengeService(
      std::unique_ptr<KeyChallengeService> key_challenge_service,
      const std::string& account_id) = 0;

  // Initializes ChallengeCredentialsHelper for
  // AuthBlockType::kChallengeCredential
  virtual void InitializeForChallengeCredentials(
      ChallengeCredentialsHelper* challenge_credentials_helper) = 0;

  // Returns if the AuthBlockUtility has called
  // InitializeForChallengeCredentials and has a valid
  // challenge_credentials_helper_.
  virtual bool IsChallengeCredentialReady() const = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_
