// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/loading.h"

#include <memory>
#include <string>
#include <utility>

#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/username.h"

namespace cryptohome {

bool LoadUserAuthFactorByLabel(AuthFactorDriverManager* driver_manager,
                               AuthFactorManager* manager,
                               const AuthBlockUtility& auth_block_utility,
                               const ObfuscatedUsername& obfuscated_username,
                               const std::string& factor_label,
                               user_data_auth::AuthFactor* out_auth_factor) {
  for (const auto& [label, auth_factor_type] :
       manager->ListAuthFactors(obfuscated_username)) {
    if (label == factor_label) {
      CryptohomeStatusOr<std::unique_ptr<AuthFactor>> owned_auth_factor =
          manager->LoadAuthFactor(obfuscated_username, auth_factor_type, label);
      if (!owned_auth_factor.ok() || *owned_auth_factor == nullptr) {
        return false;
      }

      AuthFactor& auth_factor = **owned_auth_factor;
      const AuthFactorDriver& factor_driver =
          driver_manager->GetDriver(auth_factor.type());

      auto auth_factor_proto = factor_driver.ConvertToProto(
          auth_factor.label(), auth_factor.metadata());
      if (!auth_factor_proto) {
        return false;
      }
      *out_auth_factor = std::move(*auth_factor_proto);
      return true;
    }
  }
  return false;
}

}  // namespace cryptohome
