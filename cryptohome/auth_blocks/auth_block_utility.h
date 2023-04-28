// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/containers/flat_set.h>
#include <brillo/secure_blob.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_challenge_service_factory_impl.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/username.h"

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

  // Returns whether the system is locked to only allow authenticating a single
  // user.
  virtual bool GetLockedToSingleUser() const = 0;

  // If the verify/prepare succeeds, |error| will be ok. Otherwise it will
  // contain an error describing the nature of the failure.
  using CryptohomeStatusCallback =
      base::OnceCallback<void(CryptohomeStatus error)>;

  // Given an AuthFactorType, attempt to prepare an auth factor for
  // authentication. Returns through the asynchronous |callback|.
  virtual void PrepareAuthFactorForAuth(
      AuthFactorType auth_factor_type,
      const ObfuscatedUsername& username,
      PreparedAuthFactorToken::Consumer callback) = 0;

  // Given an AuthFactorType, attempt to prepare an auth factor for add.
  // Returns through the asynchronous |callback|.
  virtual void PrepareAuthFactorForAdd(
      AuthFactorType auth_factor_type,
      const ObfuscatedUsername& username,
      PreparedAuthFactorToken::Consumer callback) = 0;

  // Creates KeyBlobs and AuthBlockState with the given type of AuthBlock for
  // the given input. Creating KeyBlobs means generating the KeyBlobs from
  // user input when the credentials are entered first time. Thus, Create should
  // be called to generate the KeyBlobs for adding initial key, adding key and
  // migrating a key.
  virtual void CreateKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const AuthInput& auth_input,
      AuthBlock::CreateCallback create_callback) = 0;

  // Derives KeyBlobs with the given type of AuthBlock using the passed
  // input and the AuthBlockState. Deriving KeyBlobs means generating the
  // KeyBlobs from provided input and the stored metadata for an existing
  // key. Thus Derive should be called to generate the KeyBlob for loading an
  // existing wrapped key from the disk for user authentication.
  virtual void DeriveKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const AuthInput& auth_input,
      const AuthBlockState& auth_state,
      AuthBlock::DeriveCallback derive_callback) = 0;

  // Select the correct AuthFactor that should be used for deriving the
  // KeyBlob from the candidates.
  virtual void SelectAuthFactorWithAuthBlock(
      AuthBlockType auth_block_type,
      const AuthInput& auth_input,
      std::vector<AuthFactor> auth_factors,
      AuthBlock::SelectFactorCallback select_callback) = 0;

  // This function returns the AuthBlock type for
  // AuthBlock::Create() based on the specified credential type, |tpm_| and
  //  |crypto_| status. If there's no suitable AuthBlock, returns an error.
  virtual CryptoStatusOr<AuthBlockType> GetAuthBlockTypeForCreation(
      const AuthFactorType& auth_factor_type) const = 0;

  // This function returns the AuthBlock type based on AuthBlockState.
  virtual std::optional<AuthBlockType> GetAuthBlockTypeFromState(
      const AuthBlockState& state) const = 0;

  // Returns the set of supported AuthIntents, determined from the PinWeaver
  // AuthBlockState if it is available.
  virtual base::flat_set<AuthIntent> GetSupportedIntentsFromState(
      const AuthBlockState& auth_block_state) const = 0;

  // This populates an AuthBlockState allocated by the caller.
  [[nodiscard]] virtual bool GetAuthBlockStateFromVaultKeyset(
      const std::string& label,
      const ObfuscatedUsername& obfuscated_username,
      AuthBlockState& out_state) const = 0;

  // Reads an auth block state and update the given VaultKeyset with what it
  // returns.
  virtual void AssignAuthBlockStateToVaultKeyset(
      const AuthBlockState& state, VaultKeyset& vault_keyset) const = 0;

  // Executes additional steps needed for auth block removal, using the given
  // auth block state.
  [[nodiscard]] virtual CryptohomeStatus PrepareAuthBlockForRemoval(
      const AuthBlockState& auth_block_state) = 0;

  // Generates a payload for cryptohome recovery AuthFactor authentication.
  [[nodiscard]] virtual CryptoStatus GenerateRecoveryRequest(
      const ObfuscatedUsername& obfuscated_username,
      const cryptorecovery::RequestMetadata& request_metadata,
      const brillo::Blob& epoch_response,
      const CryptohomeRecoveryAuthBlockState& state,
      const hwsec::RecoveryCryptoFrontend* recovery_hwsec,
      brillo::SecureBlob* out_recovery_request,
      brillo::SecureBlob* out_ephemeral_pub_key) const = 0;

  // Sets challenge_credentials_helper_ and key_challenge_factory_callback_
  // in AuthBlockUtility.
  virtual void InitializeChallengeCredentialsHelper(
      ChallengeCredentialsHelper* challenge_credentials_helper,
      KeyChallengeServiceFactory* key_challenge_service_factory) = 0;

  // Returns if the auth_input has valid fields to generate a
  // KeyChallengeService.
  virtual bool IsChallengeCredentialReady(
      const AuthInput& auth_input) const = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_H_
