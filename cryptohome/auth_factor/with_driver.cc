// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/with_driver.h"

#include <vector>

#include <base/containers/flat_set.h>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/flatbuffer.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/auth_session_flatbuffer.h"
#include "cryptohome/credential_verifier.h"

namespace cryptohome {

bool IntentInUserPolicyIntents(
    const AuthIntent& intent,
    std::vector<::cryptohome::enumeration::SerializedAuthIntent> auth_intents) {
  for (auto auth_intent : auth_intents) {
    if (SerializeAuthIntent(intent) == auth_intent) {
      return true;
    }
  }
  return false;
}

// Checks if the intent is enabled based on the driver and the user policy.
bool IsIntentEnabled(
    const AuthFactorDriver& driver,
    const AuthIntent& intent,
    const std::optional<SerializedUserAuthFactorTypePolicy>& user_policy) {
  // If the intent is NotConfigurable, the intent is considered enabled.
  // Otherwise, the intent is enabled if it is enabled by default and the user
  // policy has not explicitly disabled it, or if it is disabled by default but
  // the user policy has explicitly enabled it.
  switch (driver.GetIntentConfigurability(intent)) {
    case AuthFactorDriver::IntentConfigurability::kNotConfigurable:
      return true;
    case AuthFactorDriver::IntentConfigurability::kEnabledByDefault:
      return !user_policy.has_value() ||
             !IntentInUserPolicyIntents(intent, user_policy->disabled_intents);
    case AuthFactorDriver::IntentConfigurability::kDisabledByDefault:
      return user_policy.has_value() &&
             IntentInUserPolicyIntents(intent, user_policy->enabled_intents);
    default:
      return false;
  }
}

base::flat_set<AuthIntent> GetFullAuthSupportedIntents(
    const ObfuscatedUsername& username,
    const AuthFactor& auth_factor,
    AuthFactorDriverManager& driver_manager,
    const std::optional<SerializedUserAuthFactorTypePolicy>& user_policy) {
  AuthFactorDriver& driver = driver_manager.GetDriver(auth_factor.type());

  // If the hardware support for this factor is not available no intents are
  // available.
  if (!driver.IsSupportedByHardware()) {
    return {};
  }

  // If the driver supports expiration lockout, and the factor is currently
  // expired then no intents are available.
  if (driver.IsExpirationSupported()) {
    auto is_expired = driver.IsExpired(username, auth_factor);
    if (is_expired.value_or(true)) {
      return {};
    }
  }

  // If the driver supports delay or lockout, and the factor is currently locked
  // then no intents are available. If the delay lookup fails then we assume
  // that the factor is not working correctly and so is also unavailable.
  if (driver.IsDelaySupported()) {
    auto delay = driver.GetFactorDelay(username, auth_factor);
    if (!delay.ok() || delay->is_positive()) {
      return {};
    }
  }

  // If we get there than the factor is "working". We construct a set of
  // supported intents by checking which intents are supported by either full or
  // lightweight auth with either one being sufficient to consider the intent
  // available.
  base::flat_set<AuthIntent> supported_intents;
  for (AuthIntent intent : kAllAuthIntents) {
    if (driver.IsLightAuthSupported(intent) ||
        driver.IsFullAuthSupported(intent)) {
      if (IsIntentEnabled(driver, intent, user_policy)) {
        supported_intents.insert(intent);
      }
    }
  }
  return supported_intents;
}

base::flat_set<AuthIntent> GetLightAuthSupportedIntents(
    const ObfuscatedUsername& username,
    const AuthFactorType& auth_factor_type,
    AuthFactorDriverManager& driver_manager,
    const std::optional<SerializedUserAuthFactorTypePolicy>& user_policy) {
  AuthFactorDriver& driver = driver_manager.GetDriver(auth_factor_type);

  // If the hardware support for this factor is not available no intents are
  // available.
  if (!driver.IsSupportedByHardware()) {
    return {};
  }

  // Note that for factors that support verification, we don't do expiration or
  // delay because those are generally incompatible with verification.

  // Check all of the intents against lightweight auth. Technically we could
  // probably just look at verify-only but we leave this up to the driver.
  base::flat_set<AuthIntent> supported_intents;
  for (AuthIntent intent : kAllAuthIntents) {
    if (driver.IsLightAuthSupported(intent)) {
      if (IsIntentEnabled(driver, intent, user_policy)) {
        supported_intents.insert(intent);
      }
    }
  }
  return supported_intents;
}

}  // namespace cryptohome
