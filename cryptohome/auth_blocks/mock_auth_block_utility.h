// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_
#define CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_

#include "cryptohome/auth_blocks/auth_block_utility.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

class MockAuthBlockUtility : public AuthBlockUtility {
 public:
  MockAuthBlockUtility() = default;
  ~MockAuthBlockUtility() = default;

  MOCK_METHOD(bool, GetLockedToSingleUser, (), (const, override));
  MOCK_METHOD(CryptoStatus,
              CreateKeyBlobsWithAuthBlock,
              (AuthBlockType auth_block_type,
               const Credentials& credentials,
               const std::optional<brillo::SecureBlob>& reset_secret,
               AuthBlockState& out_state,
               KeyBlobs& out_key_blobs),
              (const, override));
  MOCK_METHOD(bool,
              CreateKeyBlobsWithAuthBlockAsync,
              (AuthBlockType auth_block_type,
               const AuthInput& auth_input,
               AuthBlock::CreateCallback create_callback),
              (override));
  MOCK_METHOD(CryptoStatus,
              DeriveKeyBlobsWithAuthBlock,
              (AuthBlockType auth_block_type,
               const Credentials& credentials,
               const AuthBlockState& state,
               KeyBlobs& out_key_blobs),
              (const, override));
  MOCK_METHOD(bool,
              DeriveKeyBlobsWithAuthBlockAsync,
              (AuthBlockType auth_block_type,
               const AuthInput& auth_input,
               const AuthBlockState& auth_state,
               AuthBlock::DeriveCallback derive_callback),
              (override));
  MOCK_METHOD(AuthBlockType,
              GetAuthBlockTypeForCreation,
              (const bool, const bool, const bool, const AuthFactorStorageType),
              (const, override));
  MOCK_METHOD(AuthBlockType,
              GetAuthBlockTypeForDerive,
              (const AuthBlockState& auth_state),
              (const, override));
  MOCK_METHOD(AuthBlockType,
              GetAuthBlockTypeForDerivation,
              (const std::string&, const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              GetAuthBlockStateFromVaultKeyset,
              (const std::string& label,
               const std::string& obfuscated_username,
               AuthBlockState& out_state),
              (const, override));
  MOCK_METHOD(void,
              AssignAuthBlockStateToVaultKeyset,
              (const AuthBlockState& state, VaultKeyset& vault_keyset),
              (const, override));
  MOCK_METHOD(CryptoStatus,
              CreateKeyBlobsWithAuthFactorType,
              (AuthFactorType auth_factor_type,
               const AuthFactorStorageType auth_factor_storage_type,
               const AuthInput& auth_input,
               AuthBlockState& out_auth_block_state,
               KeyBlobs& out_key_blobs),
              (const, override));
  MOCK_METHOD(CryptoStatus,
              DeriveKeyBlobs,
              (const AuthInput& auth_input,
               const AuthBlockState& auth_block_state,
               KeyBlobs& out_key_blobs),
              (const, override));
  MOCK_METHOD(CryptoStatus,
              GenerateRecoveryRequest,
              (const std::string& obfuscated_username,
               const cryptorecovery::RequestMetadata& request_metadata,
               const brillo::Blob& epoch_response,
               const CryptohomeRecoveryAuthBlockState& state,
               Tpm* tpm,
               brillo::SecureBlob* out_recovery_request,
               brillo::SecureBlob* out_ephemeral_pub_key),
              (const, override));
  MOCK_METHOD(void,
              SetSingleUseKeyChallengeService,
              (std::unique_ptr<KeyChallengeService> key_challenge_service,
               const std::string& account_id),
              (override));
  MOCK_METHOD(void,
              InitializeForChallengeCredentials,
              (ChallengeCredentialsHelper* const challenge_credentials_helper),
              (override));
  MOCK_METHOD(bool, IsChallengeCredentialReady, (), (const, override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_
