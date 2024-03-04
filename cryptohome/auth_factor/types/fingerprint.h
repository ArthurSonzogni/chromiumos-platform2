// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_FINGERPRINT_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_FINGERPRINT_H_

#include <optional>

#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/common.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/username.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {

class FingerprintAuthFactorDriver final
    : public AfDriverWithType<AuthFactorType::kFingerprint>,
      public AfDriverWithBlockTypes<AuthBlockType::kFingerprint>,
      public AfDriverSupportedByStorage<AfDriverStorageConfig::kUsingUss,
                                        AfDriverKioskConfig::kNoKiosk>,
      public AfDriverFullAuthDecrypt,
      public AfDriverWithMetadata<FingerprintMetadata>,
      public AfDriverFullAuthIsRepeatable<false>,
      public AfDriverWithConfigurableIntents<
          AuthIntentSequence<AuthIntent::kVerifyOnly>,
          AuthIntentSequence<AuthIntent::kDecrypt, AuthIntent::kRestoreKey>>,
      public AfDriverNoCredentialVerifier,
      public AfDriverNoKnowledgeFactor {
 public:
  FingerprintAuthFactorDriver(
      libstorage::Platform* platform,
      Crypto* crypto,
      UssManager* uss_manager,
      AsyncInitPtr<BiometricsAuthBlockService> bio_service)
      : crypto_(crypto), uss_manager_(uss_manager), bio_service_(bio_service) {}

 private:
  bool IsSupportedByHardware() const override;
  PrepareRequirement GetPrepareRequirement(
      AuthFactorPreparePurpose purpose) const override;
  void PrepareForAdd(const AuthInput& auth_input,
                     PreparedAuthFactorToken::Consumer callback) override;
  void PrepareForAuthenticate(
      const AuthInput& auth_input,
      PreparedAuthFactorToken::Consumer callback) override;
  bool NeedsResetSecret() const override;
  bool NeedsRateLimiter() const override;
  CryptohomeStatus TryCreateRateLimiter(const ObfuscatedUsername& username,
                                        DecryptedUss& decrypted_uss) override;
  bool IsDelaySupported() const override;
  CryptohomeStatusOr<base::TimeDelta> GetFactorDelay(
      const ObfuscatedUsername& username,
      const AuthFactor& factor) const override;
  bool IsExpirationSupported() const override;
  CryptohomeStatusOr<base::TimeDelta> GetTimeUntilExpiration(
      const ObfuscatedUsername& username,
      const AuthFactor& factor) const override;
  AuthFactorLabelArity GetAuthFactorLabelArity() const override;

  std::optional<user_data_auth::AuthFactor> TypedConvertToProto(
      const CommonMetadata& common,
      const FingerprintMetadata& typed_metadata) const override;

  // Starts a fp enroll session through biod, with obtained |nonce|.
  // Intended as a callback for BiometricsAuthBlockService::GetNonce.
  void PrepareForAddOnGetNonce(PreparedAuthFactorToken::Consumer callback,
                               const AuthInput& auth_input,
                               std::optional<brillo::Blob> nonce);

  // Starts a fp auth session through biod, with obtained |nonce|.
  // Intended as a callback for BiometricsAuthBlockService::GetNonce.
  void PrepareForAuthOnGetNonce(PreparedAuthFactorToken::Consumer callback,
                                const AuthInput& auth_input,
                                std::optional<brillo::Blob> nonce);

  Crypto* crypto_;
  UssManager* uss_manager_;
  AsyncInitPtr<BiometricsAuthBlockService> bio_service_;

  base::WeakPtrFactory<FingerprintAuthFactorDriver> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_FINGERPRINT_H_
