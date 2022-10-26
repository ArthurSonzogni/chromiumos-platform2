// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_

#include <memory>
#include <optional>
#include <set>
#include <string>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"

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
                       Platform* platform,
                       std::unique_ptr<FingerprintAuthBlockService> fp_service);

  AuthBlockUtilityImpl(const AuthBlockUtilityImpl&) = delete;
  AuthBlockUtilityImpl& operator=(const AuthBlockUtilityImpl&) = delete;
  ~AuthBlockUtilityImpl() override;

  bool GetLockedToSingleUser() const override;

  bool IsAuthFactorSupported(
      AuthFactorType auth_factor_type,
      AuthFactorStorageType auth_factor_storage_type,
      const std::set<AuthFactorType>& configured_factors) const override;

  bool IsPrepareAuthFactorRequired(
      AuthFactorType auth_factor_type) const override;

  bool IsVerifyWithAuthFactorSupported(
      AuthIntent auth_intent, AuthFactorType auth_factor_type) const override;

  void PrepareAuthFactorForAuth(AuthFactorType auth_factor_type,
                                const std::string& username,
                                CryptohomeStatusCallback callback) override;

  void PrepareAuthFactorForAdd(AuthFactorType auth_factor_type,
                               const std::string& username,
                               CryptohomeStatusCallback callback) override;

  void VerifyWithAuthFactorAsync(AuthFactorType auth_factor_type,
                                 const AuthInput& auth_input,
                                 CryptohomeStatusCallback callback) override;

  CryptohomeStatus TerminateAuthFactor(
      AuthFactorType auth_factor_type) override;

  CryptoStatus CreateKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const std::optional<brillo::SecureBlob>& reset_secret,
      AuthBlockState& out_state,
      KeyBlobs& out_key_blobs) const override;

  // Creates KeyBlobs and AuthBlockState for the given credentials and returns
  // through the asynchronous create_callback.
  bool CreateKeyBlobsWithAuthBlockAsync(
      AuthBlockType auth_block_type,
      const AuthInput& auth_input,
      AuthBlock::CreateCallback create_callback) override;

  CryptoStatus DeriveKeyBlobsWithAuthBlock(
      AuthBlockType auth_block_type,
      const Credentials& credentials,
      const AuthBlockState& state,
      KeyBlobs& out_key_blobs) const override;

  // Creates KeyBlobs and AuthBlockState for the given credentials and returns
  // through the asynchronous derive_callback.
  bool DeriveKeyBlobsWithAuthBlockAsync(
      AuthBlockType auth_block_type,
      const AuthInput& auth_input,
      const AuthBlockState& auth_state,
      AuthBlock::DeriveCallback derive_callback) override;

  AuthBlockType GetAuthBlockTypeForCreation(
      const bool is_le_credential,
      const bool is_recovery,
      const bool is_challenge_credential) const override;

  // This function returns the AuthBlock type for
  // AuthBlock::Derive() based on AutBlockState.
  AuthBlockType GetAuthBlockTypeFromState(
      const AuthBlockState& state) const override;

  // Returns the set of supported AuthIntents, determined from the PinWeaver
  // AuthBlockState if it is available.
  base::flat_set<AuthIntent> GetSupportedIntentsFromState(
      const AuthBlockState& auth_block_state) const override;

  bool GetAuthBlockStateFromVaultKeyset(
      const std::string& label,
      const std::string& obfuscated_username,
      AuthBlockState& out_state) const override;

  void AssignAuthBlockStateToVaultKeyset(
      const AuthBlockState& state, VaultKeyset& vault_keyset) const override;

  CryptoStatus PrepareAuthBlockForRemoval(
      const AuthBlockState& auth_block_state) override;

  CryptoStatus GenerateRecoveryRequest(
      const std::string& obfuscated_username,
      const cryptorecovery::RequestMetadata& request_metadata,
      const brillo::Blob& epoch_response,
      const CryptohomeRecoveryAuthBlockState& state,
      hwsec::RecoveryCryptoFrontend* recovery_hwsec,
      brillo::SecureBlob* out_recovery_request,
      brillo::SecureBlob* out_ephemeral_pub_key) const override;

  void InitializeChallengeCredentialsHelper(
      ChallengeCredentialsHelper* challenge_credentials_helper,
      KeyChallengeServiceFactory* key_challenge_service_factory) override;

  bool IsChallengeCredentialReady(const AuthInput& auth_input) const override;

 private:
  // This helper function serves as a factory method to return the authblock
  // used in authentication.
  CryptoStatusOr<std::unique_ptr<SyncAuthBlock>> GetAuthBlockWithType(
      const AuthBlockType& auth_block_type) const;

  // This helper function returns an authblock with asynchronous create and
  // derive.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> GetAsyncAuthBlockWithType(
      const AuthBlockType& auth_block_type, const AuthInput& auth_input);

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
  ChallengeCredentialsHelper* challenge_credentials_helper_ = nullptr;

  // Factory of key challenge service used to generate a key_challenge_service
  // for Challenge Credentials. KeyChallengeService is tasked with contacting
  // the challenge response D-Bus service that'll provide the response once
  // we send the challenge.
  KeyChallengeServiceFactory* key_challenge_service_factory_ = nullptr;

  // Fingerprint service, used by operations that need to interact with
  // fingerprint sensors.
  std::unique_ptr<FingerprintAuthBlockService> fp_service_;

  friend class AuthBlockUtilityImplTest;
  FRIEND_TEST(AuthBlockUtilityImplTest, GetAsyncAuthBlockWithType);
  FRIEND_TEST(AuthBlockUtilityImplTest, GetAsyncAuthBlockWithTypeFail);

  // TODO(b/246576446): remove the following two lines once AuthBlockUtility
  // has the functions to set up fingerprint service state.
  FRIEND_TEST(AuthBlockUtilityImplTest, VerifyFingerprintSuccess);
  FRIEND_TEST(AuthBlockUtilityImplTest, VerifyFingerprintFailure);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_UTILITY_IMPL_H_
