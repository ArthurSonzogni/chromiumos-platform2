// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/manager.h"

#include <sys/stat.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <flatbuffers/flatbuffers.h>
#include <libhwsec-foundation/flatbuffers/basic_objects.h>
#include <libhwsec-foundation/flatbuffers/flatbuffer_secure_allocator_bridge.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/label.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

// Use rw------- for the auth factor files.
constexpr mode_t kAuthFactorFilePermissions = 0600;

// Checks if the provided `auth_factor_label` is valid and on success returns
// `AuthFactorPath()`.
CryptohomeStatusOr<base::FilePath> GetAuthFactorPathFromStringType(
    const ObfuscatedUsername& obfuscated_username,
    const std::string& auth_factor_type_string,
    const std::string& auth_factor_label) {
  if (!IsValidAuthFactorLabel(auth_factor_label)) {
    LOG(ERROR) << "Invalid auth factor label " << auth_factor_label
               << " of type " << auth_factor_type_string;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocGetAuthFactorPathInvalidLabel),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  return AuthFactorPath(obfuscated_username, auth_factor_type_string,
                        auth_factor_label);
}

// Converts `auth_factor_type` to string and on success calls
// `GetAuthFactorPathFromStringType()` method above.
CryptohomeStatusOr<base::FilePath> GetAuthFactorPath(
    const ObfuscatedUsername& obfuscated_username,
    const AuthFactorType auth_factor_type,
    const std::string& auth_factor_label) {
  const std::string type_string = AuthFactorTypeToString(auth_factor_type);
  if (type_string.empty()) {
    LOG(ERROR) << "Failed to convert auth factor type "
               << static_cast<int>(auth_factor_type) << " for factor called "
               << auth_factor_label;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocGetAuthFactorPathWrongTypeString),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  return GetAuthFactorPathFromStringType(obfuscated_username, type_string,
                                         auth_factor_label);
}
}  // namespace

AuthFactorManager::AuthFactorManager(libstorage::Platform* platform,
                                     KeysetManagement* keyset_management,
                                     UssManager* uss_manager)
    : platform_(platform),
      uss_manager_(uss_manager),
      converter_(keyset_management) {
  CHECK(platform_);
  CHECK(uss_manager_);
}

AuthFactorMap& AuthFactorManager::GetAuthFactorMap(
    const ObfuscatedUsername& username) {
  auto iter = map_of_af_maps_.lower_bound(username);
  if (iter == map_of_af_maps_.end() || iter->first != username) {
    iter = map_of_af_maps_.emplace_hint(iter, username,
                                        LoadAllAuthFactors(username));
  }
  return iter->second;
}

void AuthFactorManager::DiscardAuthFactorMap(
    const ObfuscatedUsername& username) {
  map_of_af_maps_.erase(username);
}

void AuthFactorManager::DiscardAllAuthFactorMaps() {
  map_of_af_maps_.clear();
}

CryptohomeStatus AuthFactorManager::SaveAuthFactorFile(
    const ObfuscatedUsername& obfuscated_username,
    const AuthFactor& auth_factor) {
  CryptohomeStatusOr<base::FilePath> file_path = GetAuthFactorPath(
      obfuscated_username, auth_factor.type(), auth_factor.label());
  if (!file_path.ok()) {
    LOG(ERROR) << "Failed to get auth factor path in Save.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerGetPathFailedInSave),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  // Create a flatbuffer to be persisted.
  std::optional<brillo::Blob> flatbuffer =
      SerializedAuthFactor{.auth_block_state = auth_factor.auth_block_state(),
                           .metadata = auth_factor.metadata().metadata,
                           .common_metadata = auth_factor.metadata().common}
          .Serialize();

  if (!flatbuffer.has_value()) {
    LOG(ERROR) << "Failed to serialize auth factor " << auth_factor.label()
               << " of type " << AuthFactorTypeToString(auth_factor.type());
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerSerializeFailedInSave),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  // Write the file.
  if (!platform_->WriteFileAtomicDurable(*file_path, *flatbuffer,
                                         kAuthFactorFilePermissions)) {
    LOG(ERROR) << "Failed to persist auth factor " << auth_factor.label()
               << " of type " << AuthFactorTypeToString(auth_factor.type())
               << " for " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerWriteFailedInSave),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  return OkStatus<CryptohomeError>();
}

CryptohomeStatus AuthFactorManager::DeleteAuthFactorFile(
    const ObfuscatedUsername& obfuscated_username,
    const AuthFactor& auth_factor) {
  CryptohomeStatusOr<base::FilePath> file_path = GetAuthFactorPath(
      obfuscated_username, auth_factor.type(), auth_factor.label());
  if (!file_path.ok()) {
    LOG(ERROR) << "Failed to get auth factor path in Save.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerGetPathFailedInDelete),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  // Remove the file.
  if (!platform_->DeleteFileSecurely(*file_path)) {
    LOG(WARNING) << "Failed to securely delete from disk auth factor "
                 << auth_factor.label() << " of type "
                 << AuthFactorTypeToString(auth_factor.type()) << " for "
                 << obfuscated_username
                 << ". Attempting to delete without zeroization.";
    if (!platform_->DeleteFile(*file_path)) {
      LOG(ERROR) << "Failed to delete from disk auth factor "
                 << auth_factor.label() << " of type "
                 << AuthFactorTypeToString(auth_factor.type()) << " for "
                 << obfuscated_username;
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerDeleteFailedInDelete),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kRetry, PossibleAction::kReboot}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
  }
  LOG(INFO) << "Deleted from disk auth factor label: " << auth_factor.label();

  // Remove the checksum file and only log warnings if the removal failed.
  base::FilePath auth_factor_checksum_path =
      file_path->AddExtension(libstorage::kChecksumExtension);
  if (!platform_->DeleteFileSecurely(auth_factor_checksum_path)) {
    LOG(WARNING)
        << "Failed to securely delete checksum file from disk for auth factor "
        << auth_factor.label() << " of type "
        << AuthFactorTypeToString(auth_factor.type()) << " for "
        << obfuscated_username << ". Attempting to delete without zeroization.";
    if (!platform_->DeleteFile(auth_factor_checksum_path)) {
      LOG(WARNING)
          << "Failed to delete checksum file from disk for auth factor "
          << auth_factor.label() << " of type "
          << AuthFactorTypeToString(auth_factor.type()) << " for "
          << obfuscated_username;
    }
  }
  return OkStatus<CryptohomeError>();
}

CryptohomeStatusOr<AuthFactor> AuthFactorManager::LoadAuthFactor(
    const ObfuscatedUsername& obfuscated_username,
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label) {
  CryptohomeStatusOr<base::FilePath> file_path = GetAuthFactorPath(
      obfuscated_username, auth_factor_type, auth_factor_label);
  if (!file_path.ok()) {
    LOG(ERROR) << "Failed to get auth factor path in Load.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerGetPathFailedInLoad),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  brillo::Blob file_contents;
  if (!platform_->ReadFile(file_path.value(), &file_contents)) {
    LOG(ERROR) << "Failed to load persisted auth factor " << auth_factor_label
               << " of type " << AuthFactorTypeToString(auth_factor_type)
               << " for " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerReadFailedInLoad),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  // This check is redundant to the flatbuffer parsing below, but we check it
  // here in order to distinguish "empty file" from "corrupted file" in metrics
  // and logs.
  if (file_contents.empty()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerEmptyReadInLoad),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  auto serialized_factor = SerializedAuthFactor::Deserialize(file_contents);
  if (!serialized_factor.has_value()) {
    LOG(ERROR) << "Failed to parse persisted auth factor " << auth_factor_label
               << " of type " << AuthFactorTypeToString(auth_factor_type)
               << " for " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerParseFailedInLoad),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  return AuthFactor(
      auth_factor_type, auth_factor_label,
      AuthFactorMetadata{.common = serialized_factor.value().common_metadata,
                         .metadata = serialized_factor.value().metadata},
      serialized_factor.value().auth_block_state);
}

AuthFactorManager::LabelToTypeMap AuthFactorManager::ListAuthFactors(
    const ObfuscatedUsername& obfuscated_username) {
  LabelToTypeMap label_to_type_map;

  std::unique_ptr<libstorage::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(AuthFactorsDirPath(obfuscated_username),
                                   /*recursive=*/false,
                                   base::FileEnumerator::FILES));
  base::FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    const base::FilePath base_name = next_path.BaseName();

    if (!base_name.RemoveFinalExtension().FinalExtension().empty()) {
      // Silently ignore files that have multiple extensions; to note, a
      // legitimate case of such files is the checksum file
      // ("<type>.<label>.sum").
      continue;
    }

    // Parse and sanitize the type.
    const std::string auth_factor_type_string =
        base_name.RemoveExtension().value();
    const std::optional<AuthFactorType> auth_factor_type =
        AuthFactorTypeFromString(auth_factor_type_string);
    if (!auth_factor_type.has_value()) {
      LOG(WARNING) << "Unknown auth factor type: file name = "
                   << base_name.value();
      continue;
    }

    // Parse and sanitize the label. Note that `FilePath::Extension()` returns a
    // string with a leading dot.
    const std::string extension = base_name.Extension();
    if (extension.length() <= 1 ||
        extension[0] != base::FilePath::kExtensionSeparator) {
      LOG(WARNING) << "Missing auth factor label: file name = "
                   << base_name.value();
      continue;
    }
    const std::string auth_factor_label = extension.substr(1);
    if (!IsValidAuthFactorLabel(auth_factor_label)) {
      LOG(WARNING) << "Invalid auth factor label: file name = "
                   << base_name.value();
      continue;
    }

    // Check for label clashes.
    if (label_to_type_map.count(auth_factor_label)) {
      const AuthFactorType previous_type = label_to_type_map[auth_factor_label];
      LOG(WARNING) << "Ignoring duplicate auth factor: label = "
                   << auth_factor_label << " type = " << auth_factor_type_string
                   << " previous type = "
                   << AuthFactorTypeToString(previous_type);
      continue;
    }

    // All checks passed - add the factor.
    label_to_type_map.insert({auth_factor_label, auth_factor_type.value()});
  }

  return label_to_type_map;
}

void AuthFactorManager::RemoveAuthFactor(
    const ObfuscatedUsername& obfuscated_username,
    const AuthFactor& auth_factor,
    AuthBlockUtility* auth_block_utility,
    StatusCallback callback) {
  auth_block_utility->PrepareAuthBlockForRemoval(
      obfuscated_username, auth_factor.auth_block_state(),
      base::BindOnce(&AuthFactorManager::RemoveAuthFactorFiles,
                     weak_factory_.GetWeakPtr(), obfuscated_username,
                     auth_factor, std::move(callback)));
}

void AuthFactorManager::UpdateAuthFactor(
    const ObfuscatedUsername& obfuscated_username,
    const std::string& auth_factor_label,
    AuthFactor& auth_factor,
    AuthBlockUtility* auth_block_utility,
    StatusCallback callback) {
  // 1. Load the old auth factor state from disk.
  CryptohomeStatusOr<AuthFactor> existing_auth_factor = LoadAuthFactor(
      obfuscated_username, auth_factor.type(), auth_factor_label);
  if (!existing_auth_factor.ok()) {
    LOG(ERROR) << "Failed to load persisted auth factor " << auth_factor_label
               << " of type " << AuthFactorTypeToString(auth_factor.type())
               << " for " << obfuscated_username << " in Update.";
    std::move(callback).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerLoadFailedInUpdate),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
            .Wrap(std::move(existing_auth_factor).err_status()));
    return;
  }

  // 2. Save auth factor to disk - the old auth factor state will be overridden
  // and accessible only from `existing_auth_factor` object.
  CryptohomeStatus save_result =
      SaveAuthFactorFile(obfuscated_username, auth_factor);
  if (!save_result.ok()) {
    LOG(ERROR) << "Failed to save auth factor " << auth_factor.label()
               << " of type " << AuthFactorTypeToString(auth_factor.type())
               << " for " << obfuscated_username << " in Update.";
    std::move(callback).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerSaveFailedInUpdate),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
            .Wrap(std::move(save_result)));
    return;
  }

  // 3. The old auth factor state was removed from disk. Call
  // `PrepareForRemoval()` to complete the removal.
  auth_block_utility->PrepareAuthBlockForRemoval(
      obfuscated_username, existing_auth_factor->auth_block_state(),
      base::BindOnce(&AuthFactorManager::LogPrepareForRemovalStatus,
                     weak_factory_.GetWeakPtr(), obfuscated_username,
                     auth_factor, std::move(callback)));
}

AuthFactorMap AuthFactorManager::LoadAllAuthFactors(
    const ObfuscatedUsername& obfuscated_username) {
  AuthFactorMap auth_factor_map;

  // Load labels for auth factors in the USS. If the USS cannot be loaded then
  // tere are no factors listed in the USS.
  std::set<std::string_view> uss_labels;
  auto encrypted_uss = uss_manager_->LoadEncrypted(obfuscated_username);
  if (encrypted_uss.ok()) {
    uss_labels = (*encrypted_uss)->WrappedMainKeyIds();
  }

  // Load all of the USS-based auth factors.
  for (const auto& [label, auth_factor_type] :
       ListAuthFactors(obfuscated_username)) {
    if (!uss_labels.contains(label)) {
      LOG(WARNING) << "Skipping auth factor which has no key in USS " << label;
      continue;
    }
    CryptohomeStatusOr<AuthFactor> auth_factor =
        LoadAuthFactor(obfuscated_username, auth_factor_type, label);
    if (!auth_factor.ok()) {
      LOG(WARNING) << "Skipping malformed auth factor " << label;
      continue;
    }
    auth_factor_map.Add(std::move(*auth_factor),
                        AuthFactorStorageType::kUserSecretStash);
  }

  // Load all the VaultKeysets and backup VaultKeysets in disk and convert
  // them to AuthFactor format.
  std::vector<std::string> migrated_labels;
  std::map<std::string, AuthFactor> vk_factor_map;
  std::map<std::string, AuthFactor> backup_factor_map;
  converter_.VaultKeysetsToAuthFactorsAndKeyLabelData(
      obfuscated_username, migrated_labels, vk_factor_map, backup_factor_map);

  // Duplicate labels are not expected on any use case. However in very rare
  // edge cases where an interrupted USS migration results in having both
  // regular VaultKeyset and USS factor in disk it is safer to use the original
  // VaultKeyset. In that case regular VaultKeyset overrides the existing
  // label in the map.
  for (auto& [unused, factor] : vk_factor_map) {
    if (auth_factor_map.Find(factor.label())) {
      LOG(WARNING) << "Unexpected duplication of label: " << factor.label()
                   << ". Regular VaultKeyset will override the AuthFactor.";
    }
    auth_factor_map.Add(std::move(factor), AuthFactorStorageType::kVaultKeyset);
  }

  return auth_factor_map;
}

void AuthFactorManager::RemoveAuthFactorFiles(
    const ObfuscatedUsername& obfuscated_username,
    const AuthFactor& auth_factor,
    base::OnceCallback<void(CryptohomeStatus)> callback,
    CryptohomeStatus status) {
  if (!status.ok()) {
    LOG(WARNING) << "Failed to prepare for removal for auth factor "
                 << auth_factor.label() << " of type "
                 << AuthFactorTypeToString(auth_factor.type()) << " for "
                 << obfuscated_username;
    std::move(callback).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthFactorManagerPrepareForRemovalFailedInRemove),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}))
            .Wrap(std::move(status)));
    return;
  }
  std::move(callback).Run(
      DeleteAuthFactorFile(obfuscated_username, auth_factor));
}

void AuthFactorManager::LogPrepareForRemovalStatus(
    const ObfuscatedUsername& obfuscated_username,
    const AuthFactor& auth_factor,
    StatusCallback callback,
    CryptohomeStatus status) {
  if (!status.ok()) {
    LOG(WARNING) << "PrepareForRemoval failed for auth factor "
                 << auth_factor.label() << " of type "
                 << AuthFactorTypeToString(auth_factor.type()) << " for "
                 << obfuscated_username << " in Update.";
    std::move(callback).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthFactorManagerPrepareForRemovalFailedInUpdate),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT)
            .Wrap(std::move(status)));
    return;
  }

  std::move(callback).Run(OkStatus<CryptohomeError>());
}

}  // namespace cryptohome
