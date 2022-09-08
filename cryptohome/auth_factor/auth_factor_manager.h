// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MANAGER_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/platform.h"

namespace cryptohome {

// Manages the persistently stored auth factors.
//
// The basic assumption is that each factor has a unique label (among all
// factors configured for a given user).
class AuthFactorManager final {
 public:
  // Mapping between auth factor label and type.
  using LabelToTypeMap = std::map<std::string, AuthFactorType>;

  // `platform` is an unowned pointer that must outlive this object.
  explicit AuthFactorManager(Platform* platform);

  AuthFactorManager(const AuthFactorManager&) = delete;
  AuthFactorManager& operator=(const AuthFactorManager&) = delete;

  ~AuthFactorManager();

  // Serializes and persists as a file the given auth factor in the user's data
  // vault.
  CryptohomeStatus SaveAuthFactor(const std::string& obfuscated_username,
                                  const AuthFactor& auth_factor);

  // Loads from the auth factor with the given type and label from the file in
  // the user's data vault.
  CryptohomeStatusOr<std::unique_ptr<AuthFactor>> LoadAuthFactor(
      const std::string& obfuscated_username,
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label);

  // Loads the list of configured auth factors from the user's data vault.
  LabelToTypeMap ListAuthFactors(const std::string& obfuscated_username);

  // Removes the auth factor:
  // 1. Calls PrepareForRemoval() on the AuthBlock. A failure in
  // `PrepareForRemoval()` aborts the auth factor removal from disk.
  // 2. Removes the file containing state (AuthBlockState) of the given auth
  // factor from the user's data vault.
  CryptohomeStatus RemoveAuthFactor(const std::string& obfuscated_username,
                                    AuthFactor& auth_factor,
                                    AuthBlockUtility* auth_block_utility);

  // Updates the auth factor:
  // 1. Removes the auth factor with the given `auth_factor.type()` and
  // `auth_factor_label`.
  // 2. Saves the new auth factor on disk.
  // 3. Calls PrepareForRemoval() on the AuthBlock.
  // Unlike calling `RemoveAuthFactor()`+`SaveAuthFactor()`, this operation is
  // atomic, to the extent possible - it makes sure that we don't end up with no
  // auth factor available.
  CryptohomeStatus UpdateAuthFactor(const std::string& obfuscated_username,
                                    const std::string& auth_factor_label,
                                    AuthFactor& auth_factor,
                                    AuthBlockUtility* auth_block_utility);

 private:
  // Unowned pointer that must outlive this object.
  Platform* const platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MANAGER_H_
