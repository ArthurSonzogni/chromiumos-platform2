// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/le_credential_manager.h"

namespace cryptohome {

class FingerprintAuthBlock : public AuthBlock {
 public:
  // Implement the GenericAuthBlock concept.
  static constexpr auto kType = AuthBlockType::kFingerprint;
  using StateType = FingerprintAuthBlockState;
  static CryptoStatus IsSupported(
      Crypto& crypto,
      base::RepeatingCallback<BiometricsAuthBlockService*()>&
          bio_service_getter);

  FingerprintAuthBlock(LECredentialManager* le_manager,
                       BiometricsAuthBlockService* service);

  FingerprintAuthBlock(const FingerprintAuthBlock&) = delete;
  FingerprintAuthBlock& operator=(const FingerprintAuthBlock&) = delete;

  void Create(const AuthInput& auth_input, CreateCallback callback) override;

  void Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              DeriveCallback callback) override;

  CryptohomeStatus PrepareForRemoval(const AuthBlockState& state) override;

 private:
  // Creates a rate-limiter PinWeaver leaf and returns the label of the created
  // leaf.
  CryptoStatusOr<uint64_t> CreateRateLimiter(
      const ObfuscatedUsername& obfuscated_username,
      const brillo::SecureBlob& reset_secret);

  // Continue creating the KeyBlobs after receiving CreateCredential reply. This
  // is used as the callback of BiometricsAuthBlockService::CreateCredential.
  void ContinueCreate(
      CreateCallback callback,
      const ObfuscatedUsername& obfuscated_username,
      const brillo::SecureBlob& reset_secret,
      std::optional<uint64_t> created_label,
      CryptohomeStatusOr<BiometricsAuthBlockService::OperationOutput> output);

  LECredentialManager* le_manager_;
  BiometricsAuthBlockService* service_;
  base::WeakPtrFactory<FingerprintAuthBlock> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_
