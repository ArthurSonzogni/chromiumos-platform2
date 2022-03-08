// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_VAULT_KEYSET_CONVERTER_H_
#define CRYPTOHOME_AUTH_FACTOR_VAULT_KEYSET_CONVERTER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"

namespace cryptohome {

// This class contains the methods to convert an AuthFactor data to a
// VaultKeyset data and to convert on-disk VaultKeysets data to AuthFactor data.
class AuthFactorVaultKeysetConverter {
 public:
  // Unowned pointer |keyset_management| should outlive the lifetime of the
  // AuthFactorVaultKeysetConverter object.
  explicit AuthFactorVaultKeysetConverter(KeysetManagement* keyset_management_);
  AuthFactorVaultKeysetConverter(const AuthFactorVaultKeysetConverter&) =
      delete;
  AuthFactorVaultKeysetConverter& operator=(
      const AuthFactorVaultKeysetConverter&) = delete;
  ~AuthFactorVaultKeysetConverter();

  // Returns all the existing VaultKeyset data on disk mapped to their labels
  // and converted into AuthFactor format.
  user_data_auth::CryptohomeErrorCode VaultKeysetsToAuthFactors(
      const std::string& username,
      std::map<std::string, std::unique_ptr<AuthFactor>>&
          out_label_to_auth_factor);

  // Takes a label, which was sent from an AuthFactor API, find the VaultKeyset
  // identified with that label and returns its KeyData.
  user_data_auth::CryptohomeErrorCode PopulateKeyDataForVK(
      const std::string& username,
      const std::string& auth_factor_label,
      KeyData& out_vk_key_data);

 private:
  // Unowned pointer.
  KeysetManagement* const keyset_management_;
};

}  // namespace cryptohome
#endif  // CRYPTOHOME_AUTH_FACTOR_VAULT_KEYSET_CONVERTER_H_
