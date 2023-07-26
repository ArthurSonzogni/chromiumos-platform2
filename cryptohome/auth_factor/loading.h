// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_LOADING_H_
#define CRYPTOHOME_AUTH_FACTOR_LOADING_H_

#include <string>

#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/username.h"

namespace cryptohome {

// Gets AuthFactor for a given user and label. Returns false if the
// corresponding AuthFactor does not exist.
bool LoadUserAuthFactorByLabel(AuthFactorDriverManager* driver_manager,
                               AuthFactorManager* manager,
                               const AuthBlockUtility& auth_block_utility,
                               const ObfuscatedUsername& obfuscated_username,
                               const std::string& factor_label,
                               user_data_auth::AuthFactor* out_auth_factor);

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_LOADING_H_
