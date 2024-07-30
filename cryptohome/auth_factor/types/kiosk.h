// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_KIOSK_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_KIOSK_H_

#include <optional>

#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/common.h"
#include "cryptohome/auth_factor/types/password.h"
#include "cryptohome/auth_session/intent.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {

class KioskAuthFactorDriver final
    : public AfDriverWithType<AuthFactorType::kKiosk>,
      public AfDriverWithPasswordBlockTypes,
      public AfDriverSupportedByStorage<AfDriverStorageConfig::kNoChecks,
                                        AfDriverKioskConfig::kOnlyKiosk>,
      public AfDriverWithMetadata<KioskMetadata>,
      public AfDriverNoPrepare,
      public AfDriverFullAuthDecrypt,
      public AfDriverFullAuthIsRepeatable<false>,
      public AfDriverWithConfigurableIntents<AuthIntentSequence<>,
                                             AuthIntentSequence<>>,
      public AfDriverNoCredentialVerifier,
      public AfDriverNoDelay,
      public AfDriverNoExpiration,
      public AfDriverNoRateLimiter,
      public AfDriverNoKnowledgeFactor {
 public:
  KioskAuthFactorDriver() = default;

 private:
  bool IsSupportedByHardware() const override;
  AuthFactorLabelArity GetAuthFactorLabelArity() const override;

  std::optional<user_data_auth::AuthFactor> TypedConvertToProto(
      const CommonMetadata& common,
      const KioskMetadata& typed_metadata) const override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_KIOSK_H_
