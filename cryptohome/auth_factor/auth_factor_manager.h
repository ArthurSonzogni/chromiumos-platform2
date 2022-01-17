// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MANAGER_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MANAGER_H_

#include <map>
#include <string>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
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
  bool SaveAuthFactor(const std::string& obfuscated_username,
                      const AuthFactor& auth_factor);

  // Loads the list of configured auth factors from the user's data vault.
  LabelToTypeMap ListAuthFactors(const std::string& obfuscated_username);

 private:
  // Unowned pointer that must outlive this object.
  Platform* const platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_MANAGER_H_
