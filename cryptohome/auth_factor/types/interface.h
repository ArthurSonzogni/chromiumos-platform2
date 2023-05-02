// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_

#include <memory>
#include <optional>
#include <set>
#include <string>

#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "base/containers/span.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

// Defines a general interface that implements utility operations for
// interacting with an AuthFactor. This will be subclassed by a separate
// implementation for each AuthFactorType.
class AuthFactorDriver {
 public:
  explicit AuthFactorDriver(AuthFactorType type) : type_(type) {}

  AuthFactorDriver(const AuthFactorDriver&) = delete;
  AuthFactorDriver& operator=(const AuthFactorDriver&) = delete;

  virtual ~AuthFactorDriver() = default;

  // The type of factor the driver implements.
  AuthFactorType type() const { return type_; }

  // The underlying auth block types that the factor uses. The span lists them
  // in priority order, with the first element being the most preferred block
  // type to use.
  virtual base::span<const AuthBlockType> block_types() const = 0;

  // Indicates if the factor is supported based on a combination of the type of
  // auth factor storage being used, the currently configured factors, and the
  // available underlying hardware.
  virtual bool IsSupported(
      AuthFactorStorageType storage_type,
      const std::set<AuthFactorType>& configured_factors) const = 0;

  // Indicates if the factor requires the use of a Prepare operation before it
  // can be added or authenticated.
  virtual bool IsPrepareRequired() const = 0;

  // Indicates if the factor supports creating credential verifiers for a given
  // intent. Note that this only indicates that the driver software support is
  // present; this does not indicate that underlying firmware or hardware
  // support (if required) is available.
  virtual bool IsVerifySupported(AuthIntent auth_intent) const = 0;

  // Creates a credential verifier for the specified type and input. Returns
  // null on failure or if verifiers are not supported by the driver.
  virtual std::unique_ptr<CredentialVerifier> CreateCredentialVerifier(
      const std::string& auth_factor_label,
      const AuthInput& auth_input) const = 0;

  // This returns if a type is PinWeaver backed, and thus needs a reset secret.
  virtual bool NeedsResetSecret() const = 0;

  // This returns if a type is PinWeaver rate-limiter backed.
  virtual bool NeedsRateLimiter() const = 0;

  // Return an enum indicating the label arity of the auth factor (e.g. does the
  // factor support single-label authentication or multi-label authentication).
  virtual AuthFactorLabelArity GetAuthFactorLabelArity() const = 0;

  // Attempt to construct the D-Bus API proto for an AuthFactor using the given
  // metadata and label. Returns null if the conversion fails.
  virtual std::optional<user_data_auth::AuthFactor> ConvertToProto(
      const std::string& label, const AuthFactorMetadata& metadata) const = 0;

 private:
  const AuthFactorType type_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_
