// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor_vault_keyset_converter.h"

#include <base/check.h>
#include <brillo/cryptohome.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

namespace {

// Construct the AuthFactor metadata based on AuthFactor type.
bool GetAuthFactorMetadataWithType(const AuthFactorType& type,
                                   AuthFactorMetadata& metadata) {
  if (type == AuthFactorType::kPassword) {
    metadata.metadata = PasswordAuthFactorMetadata();
    return true;
  }
  return false;
}

// Returns the AuthFactor type mapped from the input VaultKeyset.
AuthFactorType VaultKeysetTypeToAuthFactorType(int32_t vk_flags) {
  AuthBlockType auth_block_type = AuthBlockType::kMaxValue;
  if (!FlagsToAuthBlockType(vk_flags, auth_block_type)) {
    LOG(ERROR) << "Failed to get the AuthBlock type for AuthFactor convertion.";
    return AuthFactorType::kUnspecified;
  }
  // For VaultKeysets password type maps to various wrapping methods.
  if (auth_block_type == AuthBlockType::kDoubleWrappedCompat ||
      auth_block_type == AuthBlockType::kTpmBoundToPcr ||
      auth_block_type == AuthBlockType::kTpmNotBoundToPcr ||
      auth_block_type == AuthBlockType::kLibScryptCompat ||
      auth_block_type == AuthBlockType::kTpmEcc) {
    return AuthFactorType::kPassword;
  }

  return AuthFactorType::kUnspecified;
}

// Returns the AuthFactor object converted from the input VaultKeyset.
std::optional<AuthFactor> ConvertToAuthFactor(const VaultKeyset& vk) {
  AuthBlockState auth_block_state;
  if (!GetAuthBlockState(vk, auth_block_state /*out*/)) {
    return std::nullopt;
  }

  // If the VaultKeyset label is empty an artificial label legacy<index> is
  // returned.
  std::string label = vk.GetLabel();
  if (!IsValidAuthFactorLabel(label)) {
    return std::nullopt;
  }

  AuthFactorType auth_factor_type =
      VaultKeysetTypeToAuthFactorType(vk.GetFlags());
  if (auth_factor_type == AuthFactorType::kUnspecified) {
    return std::nullopt;
  }

  AuthFactorMetadata metadata;
  if (!GetAuthFactorMetadataWithType(auth_factor_type, metadata)) {
    return std::nullopt;
  }

  return AuthFactor(auth_factor_type, label, metadata, auth_block_state);
}

}  // namespace

AuthFactorVaultKeysetConverter::AuthFactorVaultKeysetConverter(
    KeysetManagement* keyset_management)
    : keyset_management_(keyset_management) {
  DCHECK(keyset_management_);
}
AuthFactorVaultKeysetConverter::~AuthFactorVaultKeysetConverter() = default;

user_data_auth::CryptohomeErrorCode
AuthFactorVaultKeysetConverter::VaultKeysetsToAuthFactors(
    const std::string& username,
    std::vector<AuthFactor>& out_auth_factor_list) {
  out_auth_factor_list.clear();

  std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(username);
  std::vector<int> keyset_indices;
  if (!keyset_management_->GetVaultKeysets(obfuscated_username,
                                           &keyset_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated_username;
    return user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }

  for (int index : keyset_indices) {
    std::unique_ptr<VaultKeyset> vk =
        keyset_management_->LoadVaultKeysetForUser(obfuscated_username, index);
    if (!vk) {
      continue;
    }
    std::optional<AuthFactor> auth_factor = ConvertToAuthFactor(*vk.get());
    if (auth_factor.has_value()) {
      out_auth_factor_list.push_back(auth_factor.value());
    }
  }

  // Differentiate between no vault keyset case and vault keysets on the disk
  // but unable to be loaded case.
  if (out_auth_factor_list.empty()) {
    return user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode
AuthFactorVaultKeysetConverter::PopulateKeyDataForVK(
    const std::string& username,
    const std::string& auth_factor_label,
    KeyData& out_vk_key_data) {
  std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(username);
  std::unique_ptr<VaultKeyset> vk = keyset_management_->GetVaultKeyset(
      obfuscated_username, auth_factor_label);
  if (!vk) {
    LOG(ERROR) << "No keyset found for the label " << obfuscated_username;
    return user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }
  out_vk_key_data = vk->GetKeyData();

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

}  // namespace cryptohome
