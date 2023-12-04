// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <base/containers/span.h>
#include <base/time/time.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/prepare_purpose.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"

namespace cryptohome {

// Defines a general interface that implements utility operations for
// interacting with an AuthFactor. This will be subclassed by a separate
// implementation for each AuthFactorType.
class AuthFactorDriver {
 public:
  AuthFactorDriver() = default;

  AuthFactorDriver(const AuthFactorDriver&) = delete;
  AuthFactorDriver& operator=(const AuthFactorDriver&) = delete;

  virtual ~AuthFactorDriver() = default;

  // The type of factor the driver implements.
  virtual AuthFactorType type() const = 0;

  // The underlying auth block types that the factor uses. The span lists them
  // in priority order, with the first element being the most preferred block
  // type to use.
  virtual base::span<const AuthBlockType> block_types() const = 0;

  // Indicates if the factor is supported by the current hardware. This should
  // things like along the lines of "is the necessary hardware present", "does
  // it have the right firmware", "is it running".
  virtual bool IsSupportedByHardware() const = 0;

  // Indicates if the factor is supported by the current storage configuration.
  // This depends on both what type of storage is in use, and what other factors
  // already exist.
  virtual bool IsSupportedByStorage(
      const std::set<AuthFactorStorageType>& configured_storage_types,
      const std::set<AuthFactorType>& configured_factors) const = 0;

  // Indicates if the factor requires the use of a Prepare operation before it
  // can be added or authenticated.
  // Specifies how the Prepare operation should be called for a given
  // AuthFactorPreparePurpose of the given auth factor type.
  enum class PrepareRequirement {
    // Prepare operation isn't needed for the given purpose.
    kNone,
    // There are 2 cases we return |kOnce| because they need the same behavior:
    // 1. Each prepare session the client starts will only correspond to a
    // single actual operation of the given purpose. E.g., fingerprint
    // enrollment.
    // 2. Completing the Prepare operation supports multiple upcoming operations
    // of the given purpose. E.g., legacy fingerprint auth.
    kOnce,
    // Completing the Prepare operation only supports 1 upcoming operation of
    // the given purpose. E.g., fingerprint auth.
    kEach,
  };
  virtual PrepareRequirement GetPrepareRequirement(
      AuthFactorPreparePurpose purpose) const = 0;

  // Prepare the factor type for the addition of a new instance of this factor.
  // Returns through the asynchronous |callback|.
  virtual void PrepareForAdd(const AuthInput& auth_input,
                             PreparedAuthFactorToken::Consumer callback) = 0;

  // Prepare the factor type for authentication. Returns through the
  // asynchronous |callback|.
  virtual void PrepareForAuthenticate(
      const AuthInput& auth_input,
      PreparedAuthFactorToken::Consumer callback) = 0;

  // Specifies if the factor supports the given intent when doing either full or
  // lightweight authentication. The full authentication is when you do a
  // complete Authenticate sequence with the factor's underlying auth block
  // while the lightweight authentication is done via a CredentialVerifier.
  virtual bool IsFullAuthSupported(AuthIntent auth_intent) const = 0;
  virtual bool IsLightAuthSupported(AuthIntent auth_intent) const = 0;

  // Specifies if the factor supports repeating the AuthenticateAuthFactor
  // request with full auth that is transparent to the user (i.e., shouldn't ask
  // the user to perform auth again, like pressing the FP sensor twice for FP
  // factor). This is usually `true` for knowledge factors because we can reuse
  // the user-provided input.
  // This is used in lightweight authentication: it has lower latency, but can't
  // reset the LE credentials. Therefore, we want to perform another full auth
  // after the lightweight auth if the factor supports that.
  virtual bool IsFullAuthRepeatable() const = 0;

  // Specifies if the given intent is configurable for this driver. In general
  // any factor which is configurable should be supported (it doesn't make sense
  // to enable or disable an unsupported intent) but not-configurable intents
  // can be both supported (and so "always available") or unsupported (and so
  // "never available").
  enum class IntentConfigurability {
    kNotConfigurable,
    kEnabledByDefault,
    kDisabledByDefault,
  };
  virtual IntentConfigurability GetIntentConfigurability(
      AuthIntent auth_intent) const = 0;

  // The capability of resetting LE credentials for this auth factor. The
  // capability depends on strength of the auth factor (which auth factor tier
  // it falls in).
  enum class ResetCapability {
    kNoReset,
    kResetWrongAttemptsOnly,
    kResetWrongAttemptsAndExpiration,
  };
  virtual ResetCapability GetResetCapability() const = 0;

  // Creates a credential verifier for the specified type and input. Returns
  // null on failure or if verifiers are not supported by the driver.
  virtual std::unique_ptr<CredentialVerifier> CreateCredentialVerifier(
      const std::string& auth_factor_label,
      const AuthInput& auth_input) const = 0;

  // This returns if a type needs a reset secret.
  virtual bool NeedsResetSecret() const = 0;

  // This returns if a type is rate-limiter backed.
  virtual bool NeedsRateLimiter() const = 0;

  // This checks if the rate-limiter of |username| for this factor exists. And
  // if not, tries to create it and persist it into the USS.
  virtual CryptohomeStatus TryCreateRateLimiter(
      const ObfuscatedUsername& username, DecryptedUss& decrypted_uss) = 0;

  // This returns if a type supports delayed availability.
  virtual bool IsDelaySupported() const = 0;

  // Given an AuthFactor instance, attempt to determine how long the current
  // availability delay is. Returns a not-OK status if the delay cannot be
  // determined or the type does not support delay.
  virtual CryptohomeStatusOr<base::TimeDelta> GetFactorDelay(
      const ObfuscatedUsername& username, const AuthFactor& factor) const = 0;

  // This returns if a type supports availability expiration.
  virtual bool IsExpirationSupported() const = 0;

  // Given an AuthFactor instance, attempt to determine whether it is expired.
  // Returns a not-OK status if the expiration cannot be determined or the type
  // does not support expiration.
  virtual CryptohomeStatusOr<bool> IsExpired(const ObfuscatedUsername& username,
                                             const AuthFactor& factor) = 0;

  // Return an enum indicating the label arity of the auth factor (e.g. does the
  // factor support single-label authentication or multi-label authentication).
  virtual AuthFactorLabelArity GetAuthFactorLabelArity() const = 0;

  // Attempt to construct the D-Bus API proto for an AuthFactor using the given
  // metadata and label. Returns null if the conversion fails.
  virtual std::optional<user_data_auth::AuthFactor> ConvertToProto(
      const std::string& label, const AuthFactorMetadata& metadata) const = 0;

  // If the auth factor is qualified as a lock screen knowledge factor (meaning
  // it can generate recoverable keys that allow other devices to recover using
  // the same knowledge factor input), get the factor type. Otherwise, returns
  // nullopt.
  virtual std::optional<LockScreenKnowledgeFactorType>
  GetLockScreenKnowledgeFactorType() const = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_
