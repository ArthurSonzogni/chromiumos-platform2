// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/loading.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/auth_session_protobuf.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/platform.h"
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

AuthFactorMap LoadAuthFactorMap(bool is_uss_migration_enabled,
                                const ObfuscatedUsername& obfuscated_username,
                                Platform& platform,
                                AuthFactorVaultKeysetConverter& converter,
                                AuthFactorManager& manager) {
  AuthFactorMap auth_factor_map;

  // Load all the VaultKeysets and backup VaultKeysets in disk and convert
  // them to AuthFactor format.
  std::map<std::string, std::unique_ptr<AuthFactor>> backup_factor_map;
  std::map<std::string, std::unique_ptr<AuthFactor>> vk_factor_map;
  std::vector<std::string> migrated_labels;

  converter.VaultKeysetsToAuthFactorsAndKeyLabelData(
      obfuscated_username, migrated_labels, vk_factor_map, backup_factor_map);
  // Load the USS AuthFactors.
  std::map<std::string, std::unique_ptr<AuthFactor>> uss_factor_map =
      manager.LoadAllAuthFactors(obfuscated_username);

  // UserSecretStash is enabled: merge VaultKeyset-AuthFactors with
  // USS-AuthFactors
  if (IsUserSecretStashExperimentEnabled(&platform)) {
    for (auto& [unused, factor] : uss_factor_map) {
      auth_factor_map.Add(std::move(factor),
                          AuthFactorStorageType::kUserSecretStash);
    }
    // If USS migration is disabled, but USS is still enabled only the migrated
    // AuthFactors should be rolled back. Override the AuthFactor with the
    // migrated VaultKeyset in this case.
    if (!is_uss_migration_enabled) {
      for (auto migrated_label : migrated_labels) {
        auto backup_factor_iter = backup_factor_map.find(migrated_label);
        if (backup_factor_iter != backup_factor_map.end()) {
          auth_factor_map.Add(std::move(backup_factor_iter->second),
                              AuthFactorStorageType::kVaultKeyset);
        }
      }
    }
  } else {
    // UserSecretStash is disabled: merge VaultKeyset-AuthFactors with
    // backup-VaultKeyset-AuthFactors.
    for (auto& [unused, factor] : backup_factor_map) {
      auth_factor_map.Add(std::move(factor),
                          AuthFactorStorageType::kVaultKeyset);
    }
  }

  // Duplicate labels are not expected on any use case. However in very rare
  // edge cases where an interrupted USS migration results in having both
  // regular VaultKeyset and USS factor in disk it is safer to use the original
  // VaultKeyset. In that case regular VaultKeyset overrides the existing
  // label in the map.
  for (auto& [unused, factor] : vk_factor_map) {
    if (auth_factor_map.Find(factor->label())) {
      LOG(WARNING) << "Unexpected duplication of label: " << factor->label()
                   << ". Regular VaultKeyset will override the AuthFactor.";
    }
    auth_factor_map.Add(std::move(factor), AuthFactorStorageType::kVaultKeyset);
  }

  return auth_factor_map;
}

}  // namespace cryptohome
