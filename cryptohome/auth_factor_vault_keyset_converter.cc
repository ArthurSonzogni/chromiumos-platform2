// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor_vault_keyset_converter.h"

#include <base/check.h>
#include <brillo/cryptohome.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/key.pb.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

namespace {

// Construct the AuthFactor metadata based on AuthFactor type.
bool GetAuthFactorMetadataWithType(const AuthFactorType& type,
                                   AuthFactorMetadata& metadata) {
  switch (type) {
    case AuthFactorType::kPassword:
      metadata.metadata = PasswordAuthFactorMetadata();
      break;
    case AuthFactorType::kPin:
      metadata.metadata = PinAuthFactorMetadata();
      break;
    case AuthFactorType::kKiosk:
      metadata.metadata = KioskAuthFactorMetadata();
      break;
    default:
      return false;
  }
  return true;
}

// Returns the AuthFactor type mapped from the input VaultKeyset.
AuthFactorType VaultKeysetTypeToAuthFactorType(int32_t vk_flags,
                                               const KeyData& key_data) {
  // Kiosk is special, we need to identify it from key data and not flags.
  if (key_data.type() == KeyData::KEY_TYPE_KIOSK) {
    return AuthFactorType::kKiosk;
  }

  // Convert the VK flags to a block type and then that to a factor type.
  AuthBlockType auth_block_type = AuthBlockType::kMaxValue;
  if (!FlagsToAuthBlockType(vk_flags, auth_block_type)) {
    LOG(ERROR) << "Failed to get the AuthBlock type for AuthFactor convertion.";
    return AuthFactorType::kUnspecified;
  }
  switch (auth_block_type) {
    case AuthBlockType::kDoubleWrappedCompat:
    case AuthBlockType::kTpmBoundToPcr:
    case AuthBlockType::kTpmNotBoundToPcr:
    case AuthBlockType::kLibScryptCompat:
    case AuthBlockType::kTpmEcc:
    case AuthBlockType::kScrypt:
      return AuthFactorType::kPassword;
    case AuthBlockType::kPinWeaver:
      return AuthFactorType::kPin;
    case AuthBlockType::kChallengeCredential:  // Not yet implemented.
    case AuthBlockType::kCryptohomeRecovery:   // Never reported by a VK.
    case AuthBlockType::kMaxValue:
      return AuthFactorType::kUnspecified;
  }
}

// Returns the AuthFactor object converted from the input VaultKeyset.
std::unique_ptr<AuthFactor> ConvertToAuthFactor(const VaultKeyset& vk) {
  AuthBlockState auth_block_state;
  if (!GetAuthBlockState(vk, auth_block_state /*out*/)) {
    return nullptr;
  }

  // If the VaultKeyset label is empty an artificial label legacy<index> is
  // returned.
  std::string label = vk.GetLabel();
  if (!IsValidAuthFactorLabel(label)) {
    return nullptr;
  }

  AuthFactorType auth_factor_type =
      VaultKeysetTypeToAuthFactorType(vk.GetFlags(), vk.GetKeyDataOrDefault());
  if (auth_factor_type == AuthFactorType::kUnspecified) {
    return nullptr;
  }

  AuthFactorMetadata metadata;
  if (!GetAuthFactorMetadataWithType(auth_factor_type, metadata)) {
    return nullptr;
  }

  return std::make_unique<AuthFactor>(auth_factor_type, label, metadata,
                                      auth_block_state);
}

}  // namespace

AuthFactorVaultKeysetConverter::AuthFactorVaultKeysetConverter(
    KeysetManagement* keyset_management)
    : keyset_management_(keyset_management) {
  DCHECK(keyset_management_);
}
AuthFactorVaultKeysetConverter::~AuthFactorVaultKeysetConverter() = default;

std::unique_ptr<AuthFactor>
AuthFactorVaultKeysetConverter::VaultKeysetToAuthFactor(
    const std::string& username, const std::string& label) {
  std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(username);
  std::unique_ptr<VaultKeyset> vk =
      keyset_management_->GetVaultKeyset(obfuscated_username, label);
  if (!vk) {
    LOG(ERROR) << "No keyset found for the given label: " << label;
    return nullptr;
  }
  return ConvertToAuthFactor(*vk);
}

user_data_auth::CryptohomeErrorCode
AuthFactorVaultKeysetConverter::VaultKeysetsToAuthFactors(
    const std::string& username,
    std::map<std::string, std::unique_ptr<AuthFactor>>&
        out_label_to_auth_factor) {
  out_label_to_auth_factor.clear();

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
    std::unique_ptr<AuthFactor> auth_factor = ConvertToAuthFactor(*vk.get());
    if (auth_factor) {
      out_label_to_auth_factor.emplace(vk->GetLabel(), std::move(auth_factor));
    }
  }

  // Differentiate between no vault keyset case and vault keysets on the disk
  // but unable to be loaded case.
  if (out_label_to_auth_factor.empty()) {
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
  out_vk_key_data = vk->GetKeyDataOrDefault();

  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

user_data_auth::CryptohomeErrorCode
AuthFactorVaultKeysetConverter::AuthFactorToKeyData(
    const std::string& auth_factor_label,
    const AuthFactorType& auth_factor_type,
    KeyData& out_key_data) {
  out_key_data.set_label(auth_factor_label);

  switch (auth_factor_type) {
    case AuthFactorType::kPassword:
      out_key_data.set_type(KeyData::KEY_TYPE_PASSWORD);
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    case AuthFactorType::kPin:
      out_key_data.set_type(KeyData::KEY_TYPE_PASSWORD);
      out_key_data.mutable_policy()->set_low_entropy_credential(true);
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    case AuthFactorType::kKiosk:
      out_key_data.set_type(KeyData::KEY_TYPE_KIOSK);
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    case AuthFactorType::kCryptohomeRecovery:
    case AuthFactorType::kUnspecified:
      LOG(ERROR) << "Unimplemented AuthFactorType.";
      return user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED;
  }
}

}  // namespace cryptohome
