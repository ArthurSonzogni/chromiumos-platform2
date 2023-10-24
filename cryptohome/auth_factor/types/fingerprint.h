// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_FINGERPRINT_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_FINGERPRINT_H_

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/common.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/crypto.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/platform.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {

class FingerprintAuthFactorDriver final
    : public AfDriverWithType<AuthFactorType::kFingerprint>,
      public AfDriverWithBlockTypes<AuthBlockType::kFingerprint>,
      public AfDriverSupportedByStorage<AfDriverStorageConfig::kUsingUss,
                                        AfDriverKioskConfig::kNoKiosk>,
      public AfDriverWithMetadata<FingerprintMetadata>,
      public AfDriverFullAuthIsRepeatable<false>,
      public AfDriverResetCapability<
          AuthFactorDriver::ResetCapability::kResetWrongAttemptsAndExpiration>,
      public AfDriverWithConfigurableIntents<
          AuthIntentSequence<AuthIntent::kVerifyOnly>,
          AuthIntentSequence<AuthIntent::kDecrypt>>,
      public AfDriverNoCredentialVerifier {
 public:
  FingerprintAuthFactorDriver(
      Platform* platform,
      Crypto* crypto,
      UssStorage* uss_storage,
      AsyncInitPtr<BiometricsAuthBlockService> bio_service)
      : crypto_(crypto), uss_storage_(uss_storage), bio_service_(bio_service) {}

 private:
  bool IsSupportedByHardware() const override;
  bool IsPrepareRequired() const override;
  void PrepareForAdd(const ObfuscatedUsername& username,
                     PreparedAuthFactorToken::Consumer callback) override;
  void PrepareForAuthenticate(
      const ObfuscatedUsername& username,
      PreparedAuthFactorToken::Consumer callback) override;
  bool IsFullAuthSupported(AuthIntent auth_intent) const override;
  bool NeedsResetSecret() const override;
  bool NeedsRateLimiter() const override;
  CryptohomeStatus TryCreateRateLimiter(const ObfuscatedUsername& username,
                                        DecryptedUss& decrypted_uss) override;
  bool IsDelaySupported() const override;
  CryptohomeStatusOr<base::TimeDelta> GetFactorDelay(
      const ObfuscatedUsername& username,
      const AuthFactor& factor) const override;
  bool IsExpirationSupported() const override;
  CryptohomeStatusOr<bool> IsExpired(const ObfuscatedUsername& username,
                                     const AuthFactor& factor) override;
  AuthFactorLabelArity GetAuthFactorLabelArity() const override;

  std::optional<user_data_auth::AuthFactor> TypedConvertToProto(
      const CommonMetadata& common,
      const FingerprintMetadata& typed_metadata) const override;

  Crypto* crypto_;
  UssStorage* uss_storage_;
  AsyncInitPtr<BiometricsAuthBlockService> bio_service_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_FINGERPRINT_H_
