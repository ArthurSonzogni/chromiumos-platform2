// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_CRYPTOHOME_RECOVERY_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_CRYPTOHOME_RECOVERY_H_

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/common.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/crypto.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

class CryptohomeRecoveryAuthFactorDriver final
    : public AfDriverWithType<AuthFactorType::kCryptohomeRecovery>,
      public AfDriverWithBlockTypes<AuthBlockType::kCryptohomeRecovery>,
      public AfDriverSupportedByStorage<AfDriverStorageConfig::kUsingUss,
                                        AfDriverKioskConfig::kNoKiosk>,
      public AfDriverWithMetadata<CryptohomeRecoveryMetadata>,
      public AfDriverNoPrepare,
      public AfDriverFullAuthDecrypt,
      public AfDriverFullAuthIsRepeatable<false>,
      public AfDriverResetCapability<
          AuthFactorDriver::ResetCapability::kResetWrongAttemptsAndExpiration>,
      public AfDriverWithConfigurableIntents<AuthIntentSequence<>,
                                             AuthIntentSequence<>>,
      public AfDriverNoCredentialVerifier,
      public AfDriverNoDelay,
      public AfDriverNoExpiration,
      public AfDriverNoRateLimiter,
      public AfDriverNoKnowledgeFactor {
 public:
  explicit CryptohomeRecoveryAuthFactorDriver(Crypto* crypto)
      : crypto_(crypto) {}

 private:
  bool IsSupportedByHardware() const override;
  bool NeedsResetSecret() const override;
  AuthFactorLabelArity GetAuthFactorLabelArity() const override;

  std::optional<user_data_auth::AuthFactor> TypedConvertToProto(
      const CommonMetadata& common,
      const CryptohomeRecoveryMetadata& typed_metadata) const override;

  Crypto* crypto_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_CRYPTOHOME_RECOVERY_H_
