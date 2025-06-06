// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class contains common, reusable implementations of various subsets of
// the AuthFactorDriver interface.
//
// Using these classes generally involves using multiple inheritance, since if
// you want to use more than one (and you almost certainly do) you'll need to
// derive from all of them. In order to keep things simple and straightforward,
// we follow a few rules:
//   * All of the classes virtually inherit directly from AuthFactorDriver.
//   * The classes do final overrides of the functions they implement, to
//     prevent you from mixing conflicting implementations.
//   * If you choose to use these classes, you should subclass them and only
//     them. Do not also directly inherit from AuthFactorDriver yourself and
//     do not try to mix them in at multiple layers in a complex hierarchy.
// This keeps the usage model simple: define your driver class, subclass from
// all of the common classes you want to use, and then implement the rest of the
// virtual functions yourself.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_COMMON_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_COMMON_H_

#include <memory>
#include <optional>
#include <string>

#include <absl/container/flat_hash_set.h>
#include <base/containers/span.h>
#include <base/time/time.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_session/intent.h"
#include "cryptohome/credential_verifier.h"

namespace cryptohome {

// Common implementation of type(). Takes the type as a template parameter.
template <AuthFactorType kType>
class AfDriverWithType : public virtual AuthFactorDriver {
 protected:
  AuthFactorType type() const final { return kType; }
};

// Common implementations of block_types() for driver that support either zero
// or one block type. In the single block type case, that is the parameter for
// the template.
class AfDriverNoBlockType : public virtual AuthFactorDriver {
 protected:
  base::span<const AuthBlockType> block_types() const final { return {}; }
};
template <AuthBlockType kType>
class AfDriverWithBlockType : public virtual AuthFactorDriver {
 protected:
  base::span<const AuthBlockType> block_types() const final { return kSpan; }

 private:
  // Define a 1-length span backed by the single block type value.
  static constexpr AuthBlockType kValue = kType;
  static constexpr base::span<const AuthBlockType, 1> kSpan{&kValue, 1u};
};

// Common implementation of IsSupportedByStorage(). This implementation is
// templated on several different options that you can specify.
enum class AfDriverKioskConfig {
  kNoKiosk,    // Check that there are no kiosk factors.
  kOnlyKiosk,  // Check that there are only kiosk factors (or no factors).
};
enum class AfDriverStorageConfig {
  kNoChecks,  // Don't do any checks for storage types.
  kUsingUss,  // Check that there are USS storage types.
};
template <AfDriverStorageConfig kStorageConfig,
          AfDriverKioskConfig kKioskConfig>
class AfDriverSupportedByStorage : public virtual AuthFactorDriver {
 private:
  bool IsSupportedByStorage(const absl::flat_hash_set<AuthFactorStorageType>&
                                configured_storage_types,
                            const absl::flat_hash_set<AuthFactorType>&
                                configured_factors) const final {
    switch (kStorageConfig) {
      case AfDriverStorageConfig::kNoChecks:
        break;
      case AfDriverStorageConfig::kUsingUss:
        if (!configured_storage_types.contains(
                AuthFactorStorageType::kUserSecretStash)) {
          return false;
        }
        break;
    }
    switch (kKioskConfig) {
      case AfDriverKioskConfig::kNoKiosk:
        if (configured_factors.contains(AuthFactorType::kKiosk)) {
          return false;
        }
        break;
      case AfDriverKioskConfig::kOnlyKiosk:
        if (configured_factors.count(AuthFactorType::kKiosk) !=
            configured_factors.size()) {
          return false;
        }
        break;
    }
    return true;
  }
};

// Common implementation of ConvertToProto(). This implementation is templated
// on the variant type from AuthFactorMetadata::metadata that the driver
// requires.
template <typename MetadataType>
class AfDriverWithMetadata : public virtual AuthFactorDriver {
  std::optional<user_data_auth::AuthFactor> ConvertToProto(
      const std::string& label,
      const AuthFactorMetadata& metadata) const final {
    // Extract the factor-specific metadata and do the typed conversion.
    auto* typed_metadata = std::get_if<MetadataType>(&metadata.metadata);
    if (!typed_metadata) {
      LOG(ERROR)
          << "Cannot convert auth factor to proto, metadata has the wrong type";
      return std::nullopt;
    }
    std::optional<user_data_auth::AuthFactor> proto_factor =
        TypedConvertToProto(metadata.common, *typed_metadata);
    if (!proto_factor) {
      LOG(ERROR) << "Cannot convert auth factor to proto, type-specific "
                    "conversion failed";
      return std::nullopt;
    }
    // If we get here we were able to populate the proto with all of the
    // factor-specific data so now just fillin the common metadata and the
    // label.
    if (proto_factor->common_metadata().lockout_policy() ==
        user_data_auth::LOCKOUT_POLICY_UNKNOWN) {
      proto_factor->mutable_common_metadata()->set_lockout_policy(
          user_data_auth::LOCKOUT_POLICY_NONE);
    }
    proto_factor->set_label(label);
    proto_factor->mutable_common_metadata()->set_chromeos_version_last_updated(
        metadata.common.chromeos_version_last_updated);
    proto_factor->mutable_common_metadata()->set_chrome_version_last_updated(
        metadata.common.chrome_version_last_updated);
    proto_factor->mutable_common_metadata()->set_user_specified_name(
        metadata.common.user_specified_name);
    return proto_factor;
  }

  // Type-specific implementation of ConvertToProto that subclasses must
  // implement. Will be called by ConvertToProto if if the given metadata
  // contains the correct type-specific metadata.
  virtual std::optional<user_data_auth::AuthFactor> TypedConvertToProto(
      const CommonMetadata& common,
      const MetadataType& typed_metadata) const = 0;
};

// Common implementation of the prepare functions for drivers which do not
// support prepare.
class AfDriverNoPrepare : public virtual AuthFactorDriver {
 private:
  PrepareRequirement GetPrepareRequirement(
      AuthFactorPreparePurpose purpose) const final {
    return PrepareRequirement::kNone;
  }
  void PrepareForAdd(const PrepareInput& prepare_input,
                     PreparedAuthFactorToken::Consumer callback) final;
  void PrepareForAuthenticate(const PrepareInput& prepare_input,
                              PreparedAuthFactorToken::Consumer callback) final;
};

// Common implementations for full authentication support:
//   AfDriverFullAuthDecrypt: Supports full auth for all intents
//   AfDriverFullAuthUnsupported: Does not support full authentication
class AfDriverFullAuthDecrypt : public virtual AuthFactorDriver {
 private:
  bool IsFullAuthSupported(AuthIntent auth_intent) const final;
};

class AfDriverFullAuthUnsupported : public virtual AuthFactorDriver {
 private:
  bool IsFullAuthSupported(AuthIntent auth_intent) const final;
  bool IsFullAuthRepeatable() const final;
};

// Common implementation for support of repeating full authentication.
template <bool is_repeatable>
class AfDriverFullAuthIsRepeatable : public virtual AuthFactorDriver {
 private:
  bool IsFullAuthRepeatable() const final { return is_repeatable; }
};

// Common implementation of the verifier functions for drivers which do not
// support verifiers.
class AfDriverNoCredentialVerifier : public virtual AuthFactorDriver {
 private:
  bool IsLightAuthSupported(AuthIntent auth_intent) const final {
    return false;
  }
  std::unique_ptr<CredentialVerifier> CreateCredentialVerifier(
      const std::string& auth_factor_label,
      const AuthInput& auth_input,
      const AuthFactorMetadata& auth_factor_metadata) const final {
    return nullptr;
  }
};

// Common implementation of GetIntentConfigurability that takes two lists of
// intents as parameters: the first are the intents which are enabled by
// default, the seconds are the intents which are disabled by default.
//
// The template parameters should be AuthIntentSequence types.
template <typename EnabledIntents, typename DisabledIntents>
class AfDriverWithConfigurableIntents : public virtual AuthFactorDriver {
 private:
  IntentConfigurability GetIntentConfigurability(
      AuthIntent auth_intent) const final {
    for (AuthIntent enabled_by_default_intent : EnabledIntents::kArray) {
      if (auth_intent == enabled_by_default_intent) {
        return IntentConfigurability::kEnabledByDefault;
      }
    }
    for (AuthIntent disabled_by_default_intent : DisabledIntents::kArray) {
      if (auth_intent == disabled_by_default_intent) {
        return IntentConfigurability::kDisabledByDefault;
      }
    }
    return IntentConfigurability::kNotConfigurable;
  }
};

// Common implementation of the delay functions for drivers which do not
// support delayed availability.
class AfDriverNoDelay : public virtual AuthFactorDriver {
 private:
  bool IsDelaySupported() const final { return false; }
  CryptohomeStatusOr<base::TimeDelta> GetFactorDelay(
      const ObfuscatedUsername& username, const AuthFactor& factor) const final;
};

// Common implementation of the expiration functions for drivers which do not
// support availability expiration.
class AfDriverNoExpiration : public virtual AuthFactorDriver {
 private:
  bool IsExpirationSupported() const final { return false; }
  CryptohomeStatusOr<base::TimeDelta> GetTimeUntilExpiration(
      const ObfuscatedUsername& username, const AuthFactor& factor) const final;
};

// Common implementation of the rate-limiter functions for drivers which do not
// need rate-limiter.
class AfDriverNoRateLimiter : public virtual AuthFactorDriver {
 private:
  bool NeedsRateLimiter() const final { return false; }
  CryptohomeStatus TryCreateRateLimiter(const ObfuscatedUsername& username,
                                        DecryptedUss& decrypted_uss) final;
};

// Common implementation of GetKnowledgeFactorType(). Takes the
// KnowledgeFactorType as template parameter, with the special case
// that UNSPECIFIED is translated to nullopt. This is because returning an
// optional that either contains a valid knowledge factor type or nullopt is
// easier to handle than returning an enum that contains UNSPECIFIED.
template <KnowledgeFactorType kType>
class AfDriverWithKnowledgeFactorType : public virtual AuthFactorDriver {
 private:
  std::optional<KnowledgeFactorType> GetKnowledgeFactorType() const final {
    if (kType == KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_UNSPECIFIED) {
      return std::nullopt;
    }
    return kType;
  }
};
using AfDriverNoKnowledgeFactor = AfDriverWithKnowledgeFactorType<
    KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_UNSPECIFIED>;

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_COMMON_H_
