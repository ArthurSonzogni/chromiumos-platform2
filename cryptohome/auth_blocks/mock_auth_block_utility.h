// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_
#define CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_

#include "cryptohome/auth_blocks/auth_block_utility.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

class MockAuthBlockUtility : public AuthBlockUtility {
 public:
  MockAuthBlockUtility() = default;

  MOCK_METHOD(bool, GetLockedToSingleUser, (), (const, override));
  MOCK_METHOD(bool,
              IsAuthFactorSupported,
              (AuthFactorType,
               AuthFactorStorageType,
               const std::set<AuthFactorType>&),
              (const, override));
  MOCK_METHOD(bool,
              IsPrepareAuthFactorRequired,
              (AuthFactorType),
              (const, override));
  MOCK_METHOD(bool,
              IsVerifyWithAuthFactorSupported,
              (AuthIntent, AuthFactorType),
              (const, override));
  MOCK_METHOD(std::unique_ptr<CredentialVerifier>,
              CreateCredentialVerifier,
              (AuthFactorType, const std::string&, const AuthInput&),
              (const, override));
  MOCK_METHOD(void,
              PrepareAuthFactorForAuth,
              (AuthFactorType, const std::string&, CryptohomeStatusCallback),
              (override));
  MOCK_METHOD(void,
              PrepareAuthFactorForAdd,
              (AuthFactorType, const std::string&, CryptohomeStatusCallback),
              (override));
  MOCK_METHOD(CryptohomeStatus,
              TerminateAuthFactor,
              (AuthFactorType),
              (override));
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
              (const bool, const bool, const bool),
              (const, override));
  MOCK_METHOD(AuthBlockType,
              GetAuthBlockTypeFromState,
              (const AuthBlockState& auth_state),
              (const, override));
  MOCK_METHOD(base::flat_set<AuthIntent>,
              GetSupportedIntentsFromState,
              (const AuthBlockState& auth_state),
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
              PrepareAuthBlockForRemoval,
              (const AuthBlockState& auth_block_state),
              (override));
  MOCK_METHOD(CryptoStatus,
              GenerateRecoveryRequest,
              (const std::string& obfuscated_username,
               const cryptorecovery::RequestMetadata& request_metadata,
               const brillo::Blob& epoch_response,
               const CryptohomeRecoveryAuthBlockState& state,
               hwsec::RecoveryCryptoFrontend* recovery_hwsec,
               brillo::SecureBlob* out_recovery_request,
               brillo::SecureBlob* out_ephemeral_pub_key),
              (const, override));
  MOCK_METHOD(void,
              InitializeChallengeCredentialsHelper,
              (ChallengeCredentialsHelper * challenge_credentials_helper,
               KeyChallengeServiceFactory* key_challenge_service_factory),
              (override));
  MOCK_METHOD(bool,
              IsChallengeCredentialReady,
              (const AuthInput& auth_input),
              (const, override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_
