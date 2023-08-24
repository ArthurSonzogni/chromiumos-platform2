// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This library provides common utility operations that operate on a combination
// of an AuthFactor using the driver for that factor. All of the functions in
// here generally take as parameters and AuthFactor and an AuthFactorManager and
// then use the driver to perform some complex operation.
//
// These functions should not have any type-specific logic in them; such
// behavior should go into the drivers themselves. These functions are for
// reusing common generic patterns of composing existing driver functions.

#ifndef CRYPTOHOME_AUTH_FACTOR_WITH_DRIVER_H_
#define CRYPTOHOME_AUTH_FACTOR_WITH_DRIVER_H_

#include <base/containers/flat_set.h>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/flatbuffer_schemas/user_policy.h"

namespace cryptohome {

// Checks if the intent is enabled based on the driver and the user policy.
// If the intent is NotConfigurable, the intent is considered enabled.
// Otherwise, the intent is enabled if it is enabled by default and the user
// policy has not explicitly disabled it, or if it is disabled by default but
// the user policy has explicitly enabled it.
bool IsIntentEnabledBasedOnPolicy(
    const AuthFactorDriver& driver,
    const AuthIntent& intent,
    const SerializedUserAuthFactorTypePolicy& user_policy);

// Compute the set of auth intents available by the given AuthFactor. If the
// auth intents have been overridden by the user_policy, the user_policy will be
// returned unless the auth intent set to |kNotConfigurable| by its driver.
base::flat_set<AuthIntent> GetFullAuthAvailableIntents(
    const ObfuscatedUsername& username,
    const AuthFactor& auth_factor,
    AuthFactorDriverManager& driver_manager,
    const SerializedUserAuthFactorTypePolicy& user_policy);

// This function computes the set of intents that are supported by the given
// AuthFactorType. This function takes the override of policies into
// consideration but doesn't consider if an auth factor type is not available at
// the moment (for example if it is locked out). As availability only matters
// for full auth, this function can be used to determine available intents for
// light auth intents as well.
base::flat_set<AuthIntent> GetSupportedIntents(
    const ObfuscatedUsername& username,
    const AuthFactorType& auth_factor_type,
    AuthFactorDriverManager& driver_manager,
    const SerializedUserAuthFactorTypePolicy& user_policy,
    bool only_light_auth);

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_WITH_DRIVER_H_
