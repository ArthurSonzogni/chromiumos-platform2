// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_PASSWORD_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_PASSWORD_H_

#include <memory>
#include <optional>
#include <string>

#include <base/containers/span.h>

#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/common.h"
#include "cryptohome/auth_session/intent.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

// Implements the block supported by password factors. This is implemented as
// separate class so that it can be reused by the kiosk factor driver as well.
//
// The priority is defined based on the following:
//   1. Prefer pinweaver as the best choice, if it is both available and the
//      feature to use it is enabled.
// If pinweaver is not available then we fall back to more raw TPM options:
//   2. Favor TPM ECC as the fastest and best choice.
//   3. If ECC isn't available, prefer binding to PCR.
//   4. If PCR isn't available either, unbound TPM is our last choice.
// If cryptohome is built to allow insecure fallback then we have a final
// last resort choice:
//   5. Use the scrypt block, with no TPM
// On boards where this isn't necessary we don't even allow this option. If
// the TPM is not functioning on such a board we prefer to get the error
// rather than falling back to the less secure mechanism.
//
// Unlike the other generic "block type" implementations in common, this also
// implements the NeedsResetSecret function since its operation is tied to the
// block types that are supported.
class AfDriverWithPasswordBlockTypes : public virtual AuthFactorDriver {
 protected:
  explicit AfDriverWithPasswordBlockTypes(AsyncInitFeatures* features);

  base::span<const AuthBlockType> block_types() const final;
  bool NeedsResetSecret() const final;

 private:
  static constexpr AuthBlockType kBlockTypes[] = {
      AuthBlockType::kPinWeaver,     AuthBlockType::kTpmEcc,
      AuthBlockType::kTpmBoundToPcr, AuthBlockType::kTpmNotBoundToPcr,
#if USE_TPM_INSECURE_FALLBACK
      AuthBlockType::kScrypt,
#endif
  };

  AsyncInitFeatures* features_;
};

class PasswordAuthFactorDriver final
    : public AfDriverWithType<AuthFactorType::kPassword>,
      public AfDriverWithPasswordBlockTypes,
      public AfDriverSupportedByStorage<AfDriverStorageConfig::kNoChecks,
                                        AfDriverKioskConfig::kNoKiosk>,
      public AfDriverWithMetadata<PasswordMetadata>,
      public AfDriverNoPrepare,
      public AfDriverFullAuthDecrypt,
      public AfDriverFullAuthIsRepeatable<true>,
      public AfDriverWithConfigurableIntents<AuthIntentSequence<>,
                                             AuthIntentSequence<>>,
      public AfDriverNoDelay,
      public AfDriverNoExpiration,
      public AfDriverNoRateLimiter,
      public AfDriverWithKnowledgeFactorType<
          KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PASSWORD> {
 public:
  explicit PasswordAuthFactorDriver(AsyncInitFeatures* features);

 private:
  bool IsSupportedByHardware() const override;
  bool IsLightAuthSupported(AuthIntent auth_intent) const override;
  std::unique_ptr<CredentialVerifier> CreateCredentialVerifier(
      const std::string& auth_factor_label,
      const AuthInput& auth_input,
      const AuthFactorMetadata& auth_factor_metadata) const override;
  AuthFactorLabelArity GetAuthFactorLabelArity() const override;

  std::optional<user_data_auth::AuthFactor> TypedConvertToProto(
      const CommonMetadata& common,
      const PasswordMetadata& typed_metadata) const override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_PASSWORD_H_
