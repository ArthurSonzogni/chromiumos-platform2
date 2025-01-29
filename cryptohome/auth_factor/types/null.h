// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_NULL_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_NULL_H_

#include <optional>
#include <string>

#include <absl/container/flat_hash_set.h>

#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/common.h"
#include "cryptohome/auth_session/intent.h"

namespace cryptohome {

// Implementation of the null object pattern for auth factor drivers. Provides
// useful defaults (which fail or return something equivalent to nothing) for
// all functions implemented by a factor.
class NullAuthFactorDriver final
    : public AfDriverWithType<AuthFactorType::kUnspecified>,
      public AfDriverNoBlockType,
      public AfDriverNoPrepare,
      public AfDriverFullAuthUnsupported,
      public AfDriverWithConfigurableIntents<AuthIntentSequence<>,
                                             AuthIntentSequence<>>,
      public AfDriverNoCredentialVerifier,
      public AfDriverNoDelay,
      public AfDriverNoExpiration,
      public AfDriverNoRateLimiter,
      public AfDriverNoKnowledgeFactor {
 public:
  NullAuthFactorDriver() = default;

 private:
  bool IsSupportedByHardware() const override { return false; }
  bool IsSupportedByStorage(
      const absl::flat_hash_set<
          AuthFactorStorageType>& /*configured_storage_types*/,
      const absl::flat_hash_set<AuthFactorType>& /*configured_factors*/)
      const override {
    return false;
  }
  bool NeedsResetSecret() const override { return false; }
  AuthFactorLabelArity GetAuthFactorLabelArity() const override {
    return AuthFactorLabelArity::kNone;
  }
  std::optional<user_data_auth::AuthFactor> ConvertToProto(
      const std::string& label,
      const AuthFactorMetadata& metadata) const override {
    return std::nullopt;
  }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_NULL_H_
