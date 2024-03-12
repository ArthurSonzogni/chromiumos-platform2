// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_CRYPTOHOME_RECOVERY_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_CRYPTOHOME_RECOVERY_H_

#include <optional>

#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/common.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/crypto.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {

class CryptohomeRecoveryAuthFactorDriver final
    : public AfDriverWithType<AuthFactorType::kCryptohomeRecovery>,
      public AfDriverWithBlockTypes<AuthBlockType::kCryptohomeRecovery>,
      public AfDriverSupportedByStorage<AfDriverStorageConfig::kUsingUss,
                                        AfDriverKioskConfig::kNoKiosk>,
      public AfDriverWithMetadata<CryptohomeRecoveryMetadata>,
      public AfDriverNoPrepare,
      public AfDriverFullAuthForensics,
      public AfDriverFullAuthIsRepeatable<false>,
      public AfDriverWithConfigurableIntents<AuthIntentSequence<>,
                                             AuthIntentSequence<>>,
      public AfDriverNoCredentialVerifier,
      public AfDriverNoExpiration,
      public AfDriverNoRateLimiter,
      public AfDriverNoKnowledgeFactor {
 public:
  explicit CryptohomeRecoveryAuthFactorDriver(Crypto* crypto,
                                              libstorage::Platform* platform)
      : crypto_(crypto), platform_(platform) {}

 private:
  bool IsSupportedByHardware() const override;
  bool NeedsResetSecret() const override;
  bool IsDelaySupported() const override;
  CryptohomeStatusOr<base::TimeDelta> GetFactorDelay(
      const ObfuscatedUsername& username,
      const AuthFactor& factor) const override;
  AuthFactorLabelArity GetAuthFactorLabelArity() const override;

  std::optional<user_data_auth::AuthFactor> TypedConvertToProto(
      const CommonMetadata& common,
      const CryptohomeRecoveryMetadata& typed_metadata) const override;

  Crypto* crypto_;
  libstorage::Platform* platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_CRYPTOHOME_RECOVERY_H_
