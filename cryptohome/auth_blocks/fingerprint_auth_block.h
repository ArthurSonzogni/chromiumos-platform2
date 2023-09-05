// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_

#include <memory>
#include <optional>
#include <vector>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/pinweaver_manager/le_credential_manager.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {

class FingerprintAuthBlock : public AuthBlock {
 public:
  // Implement the GenericAuthBlock concept.
  static constexpr auto kType = AuthBlockType::kFingerprint;
  using StateType = FingerprintAuthBlockState;
  static CryptoStatus IsSupported(
      Crypto& crypto, AsyncInitPtr<BiometricsAuthBlockService> bio_service);
  static std::unique_ptr<AuthBlock> New(
      Crypto& crypto, AsyncInitPtr<BiometricsAuthBlockService> bio_service);

  FingerprintAuthBlock(LECredentialManager* le_manager,
                       BiometricsAuthBlockService* service);

  FingerprintAuthBlock(const FingerprintAuthBlock&) = delete;
  FingerprintAuthBlock& operator=(const FingerprintAuthBlock&) = delete;

  void Create(const AuthInput& auth_input, CreateCallback callback) override;

  void Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              DeriveCallback callback) override;

  void PrepareForRemoval(const ObfuscatedUsername& obfuscated_username,
                         const AuthBlockState& state,
                         StatusCallback callback) override;

  void SelectFactor(const AuthInput& auth_input,
                    std::vector<AuthFactor> auth_factors,
                    SelectFactorCallback callback) override;

 private:
  // Continue creating the KeyBlobs after receiving CreateCredential reply. This
  // is used as the callback of BiometricsAuthBlockService::CreateCredential.
  void ContinueCreate(
      CreateCallback callback,
      const ObfuscatedUsername& obfuscated_username,
      const brillo::SecureBlob& reset_secret,
      CryptohomeStatusOr<BiometricsAuthBlockService::OperationOutput> output);

  // Continue selecting up the AuthFactor after receiving MatchCredential reply.
  // This is used as the callback of
  // BiometricsAuthBlockService::MatchCredential.
  void ContinueSelect(
      SelectFactorCallback callback,
      std::vector<AuthFactor> auth_factors,
      uint64_t rate_limiter_label,
      CryptohomeStatusOr<BiometricsAuthBlockService::OperationOutput> output);

  // This is used as the callback of
  // BiometricsAuthBlockService::DeleteCredential.
  //  Processes the delete result and continue the removal by calling
  //  ContinuePrepareForRemoval if necessary.
  void OnDeleteCredentialReply(const FingerprintAuthBlockState& state,
                               StatusCallback callback,
                               BiometricsAuthBlockService::DeleteResult result);

  // Continue preparing to remove the AuthFactor after the async
  // DeleteCredential step.
  void ContinuePrepareForRemoval(const FingerprintAuthBlockState& state,
                                 StatusCallback callback);

  // Check whether the rate-limiter leaf is locked-out currently.
  bool IsLocked(uint64_t label);

  LECredentialManager* le_manager_;
  BiometricsAuthBlockService* service_;
  base::WeakPtrFactory<FingerprintAuthBlock> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_
