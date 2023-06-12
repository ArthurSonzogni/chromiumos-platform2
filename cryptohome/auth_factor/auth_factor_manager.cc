// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor_manager.h"

#include <sys/stat.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <flatbuffers/flatbuffers.h>
#include <libhwsec-foundation/flatbuffers/basic_objects.h>
#include <libhwsec-foundation/flatbuffers/flatbuffer_secure_allocator_bridge.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor_generated.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state_flatbuffer.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/platform.h"

using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

namespace {

// Use rw------- for the auth factor files.
constexpr mode_t kAuthFactorFilePermissions = 0600;

constexpr int kFlatbufferAllocatorInitialSize = 4096;

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

std::optional<cryptohome::LockoutPolicy> GetLockoutPolicy(
    auth_factor::SerializedLockoutPolicy lockout_policy) {
  switch (lockout_policy) {
    case auth_factor::SerializedLockoutPolicy::NO_LOCKOUT:
      return cryptohome::LockoutPolicy::kNoLockout;
    case auth_factor::SerializedLockoutPolicy::ATTEMPT_LIMITED:
      return cryptohome::LockoutPolicy::kAttemptLimited;
    case auth_factor::SerializedLockoutPolicy::TIME_LIMITED:
      return cryptohome::LockoutPolicy::kTimeLimited;
    case auth_factor::SerializedLockoutPolicy::UNKNOWN:
      return std::nullopt;
  }
}

auth_factor::SerializedLockoutPolicy GetSerializedLockoutPolicy(
    cryptohome::LockoutPolicy lockout_policy) {
  switch (lockout_policy) {
    case cryptohome::LockoutPolicy::kNoLockout:
      return auth_factor::SerializedLockoutPolicy::NO_LOCKOUT;
    case cryptohome::LockoutPolicy::kAttemptLimited:
      return auth_factor::SerializedLockoutPolicy::ATTEMPT_LIMITED;
    case cryptohome::LockoutPolicy::kTimeLimited:
      return auth_factor::SerializedLockoutPolicy::TIME_LIMITED;
  }
}

// Serializes the factor-specific metadata.
auth_factor::SerializedCommonMetadata SerializeCommonMetadataToOffset(
    const CommonAuthFactorMetadata& common_metadata) {
  return auth_factor::SerializedCommonMetadata{
      .chromeos_version_last_updated =
          common_metadata.chromeos_version_last_updated,
      .chrome_version_last_updated =
          common_metadata.chrome_version_last_updated,
      .lockout_policy = common_metadata.lockout_policy.has_value()
                            ? GetSerializedLockoutPolicy(
                                  common_metadata.lockout_policy.value())
                            : auth_factor::SerializedLockoutPolicy::UNKNOWN,
      .user_specified_name = common_metadata.user_specified_name};
}

auth_factor::SerializedPasswordMetadata SerializeMetadataToOffset(
    const PasswordAuthFactorMetadata& password_metadata) {
  auth_factor::SerializedPasswordMetadata metadata;
  return metadata;
}

auth_factor::SerializedPinMetadata SerializeMetadataToOffset(
    const PinAuthFactorMetadata& pin_metadata) {
  auth_factor::SerializedPinMetadata metadata;
  return metadata;
}

auth_factor::SerializedCryptohomeRecoveryMetadata SerializeMetadataToOffset(
    const CryptohomeRecoveryAuthFactorMetadata& recovery_metadata) {
  auth_factor::SerializedCryptohomeRecoveryMetadata metadata;
  return metadata;
}

auth_factor::SerializedKioskMetadata SerializeMetadataToOffset(
    const KioskAuthFactorMetadata& kiosk_metadata) {
  auth_factor::SerializedKioskMetadata metadata;
  return metadata;
}

auth_factor::SerializedSmartCardMetadata SerializeMetadataToOffset(
    const SmartCardAuthFactorMetadata& smart_card_metadata) {
  auth_factor::SerializedSmartCardMetadata metadata;

  metadata.public_key_spki_der = *smart_card_metadata.public_key_spki_der;
  return metadata;
}

auth_factor::SerializedFingerprintMetadata SerializeMetadataToOffset(
    const FingerprintAuthFactorMetadata& fingerprint_metadata) {
  auth_factor::SerializedFingerprintMetadata metadata;
  return metadata;
}

// Serializes the factor-specific metadata into the given flatbuffer builder.
// Returns the flatbuffer offset, to be used for building the outer table.
//
// Implemented by selecting the appropriate specific overload based on the
// factor type and delegating to it.
auth_factor::SerializedAuthFactorMetadata SerializeMetadataToOffset(
    const AuthFactorMetadata& metadata) {
  if (const auto* password_metadata =
          std::get_if<PasswordAuthFactorMetadata>(&metadata.metadata)) {
    return SerializeMetadataToOffset(*password_metadata);
  } else if (const auto* pin_metadata =
                 std::get_if<PinAuthFactorMetadata>(&metadata.metadata)) {
    return SerializeMetadataToOffset(*pin_metadata);
  } else if (const auto* smart_card_metadata =
                 std::get_if<SmartCardAuthFactorMetadata>(&metadata.metadata)) {
    return SerializeMetadataToOffset(*smart_card_metadata);
  } else if (const auto* recovery_metadata =
                 std::get_if<CryptohomeRecoveryAuthFactorMetadata>(
                     &metadata.metadata)) {
    return SerializeMetadataToOffset(*recovery_metadata);
  } else if (const auto* kiosk_metadata =
                 std::get_if<KioskAuthFactorMetadata>(&metadata.metadata)) {
    return SerializeMetadataToOffset(*kiosk_metadata);
  } else if (const auto* fingerprint_metadata =
                 std::get_if<FingerprintAuthFactorMetadata>(
                     &metadata.metadata)) {
    return SerializeMetadataToOffset(*fingerprint_metadata);
  }
  LOG(ERROR) << "Missing or unexpected auth factor metadata: "
             << metadata.metadata.index();
  return auth_factor::SerializedAuthFactorMetadata{};
}

// Serializes the auth factor into a flatbuffer blob. Returns null on failure.
std::optional<brillo::SecureBlob> SerializeAuthFactor(
    const AuthFactor& auth_factor) {
  hwsec_foundation::FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(kFlatbufferAllocatorInitialSize,
                                         &allocator);

  auto auth_block_state_offset =
      hwsec_foundation::ToFlatBuffer<AuthBlockState>()(
          &builder, auth_factor.auth_block_state());
  if (auth_block_state_offset.IsNull()) {
    LOG(ERROR) << "Failed to serialize auth block state";
    return std::nullopt;
  }

  auth_factor::SerializedAuthFactorMetadata serialized_metadata =
      SerializeMetadataToOffset(auth_factor.metadata());
  auto serialized_common_metadata =
      SerializeCommonMetadataToOffset(auth_factor.metadata().common);

  auth_factor::SerializedAuthFactor serialized_auth_factor{
      .auth_block_state = auth_factor.auth_block_state(),
      .metadata = serialized_metadata,
      .common_metadata = serialized_common_metadata};
  return serialized_auth_factor.Serialize();
}

void ConvertCommonMetadataFromFlatbuffer(
    const auth_factor::SerializedCommonMetadata& serialized_common_metadata,
    AuthFactorMetadata* metadata) {
  metadata->common = CommonAuthFactorMetadata{
      .chromeos_version_last_updated =
          serialized_common_metadata.chromeos_version_last_updated,
      .chrome_version_last_updated =
          serialized_common_metadata.chrome_version_last_updated,
      .lockout_policy =
          serialized_common_metadata.lockout_policy.has_value()
              ? GetLockoutPolicy(
                    serialized_common_metadata.lockout_policy.value())
              : std::nullopt,
      .user_specified_name = serialized_common_metadata.user_specified_name};
}

bool ConvertPasswordMetadataFromFlatbuffer(
    const auth_factor::SerializedPasswordMetadata& serialized_metadata,
    AuthFactorMetadata* metadata) {
  // There's no password-specific metadata currently.
  metadata->metadata = PasswordAuthFactorMetadata();
  return true;
}

bool ConvertPinMetadataFromFlatbuffer(
    const auth_factor::SerializedPinMetadata& serialized_metadata,
    AuthFactorMetadata* metadata) {
  // There's no pin-specific metadata currently.
  metadata->metadata = PinAuthFactorMetadata();
  return true;
}

bool ConvertCryptohomeRecoveryMetadataFromFlatbuffer(
    const auth_factor::SerializedCryptohomeRecoveryMetadata&
        serialized_metadata,
    AuthFactorMetadata* metadata) {
  // There's no metadata currently.
  metadata->metadata = CryptohomeRecoveryAuthFactorMetadata();
  return true;
}

bool ConvertSmartCardMetadataFromFlatbuffer(
    const auth_factor::SerializedSmartCardMetadata& serialized_metadata,
    AuthFactorMetadata* metadata) {
  metadata->metadata = SmartCardAuthFactorMetadata{
      .public_key_spki_der = serialized_metadata.public_key_spki_der};
  return true;
}

bool ConvertKioskMetadataFromFlatbuffer(
    const auth_factor::SerializedKioskMetadata& serialized_metadata,
    AuthFactorMetadata* metadata) {
  // There's no metadata currently.
  metadata->metadata = KioskAuthFactorMetadata();
  return true;
}

bool ConvertFingerprintMetadataFromFlatbuffer(
    const auth_factor::SerializedFingerprintMetadata& serialized_metadata,
    AuthFactorMetadata* metadata) {
  // There's no metadata currently.
  metadata->metadata = FingerprintAuthFactorMetadata();
  return true;
}

bool ParseAuthFactorFlatbuffer(const brillo::SecureBlob& flatbuffer,
                               AuthBlockState* auth_block_state,
                               AuthFactorMetadata* metadata) {
  flatbuffers::Verifier flatbuffer_verifier(flatbuffer.data(),
                                            flatbuffer.size());
  auto auth_factor_table =
      auth_factor::SerializedAuthFactor::Deserialize(flatbuffer);

  if (!auth_factor_table.has_value()) {
    LOG(ERROR) << "The SerializedAuthFactor flatbuffer is invalid";
    return false;
  }

  *auth_block_state = auth_factor_table->auth_block_state;

  // Extract the common metadata from the serialized data.
  ConvertCommonMetadataFromFlatbuffer(auth_factor_table->common_metadata,
                                      metadata);

  // Extract the factor-specific metadata from the serialized data.
  if (const auto* password_metadata =
          std::get_if<auth_factor::SerializedPasswordMetadata>(
              &auth_factor_table->metadata)) {
    if (!ConvertPasswordMetadataFromFlatbuffer(*password_metadata, metadata)) {
      LOG(ERROR) << "Failed to convert SerializedAuthFactor password metadata";
      return false;
    }
  } else if (const auto* pin_metadata =
                 std::get_if<auth_factor::SerializedPinMetadata>(
                     &auth_factor_table->metadata)) {
    if (!ConvertPinMetadataFromFlatbuffer(*pin_metadata, metadata)) {
      LOG(ERROR) << "Failed to convert SerializedAuthFactor pin metadata";
      return false;
    }

    // Since CommonMetadata in AuthFactor is not stored from the beginning it
    // will load the default value in CommonMetadata. To correct for the default
    // value load, we need to have this if condition for legacy PINS.
    if (!metadata->common.lockout_policy.has_value()) {
      metadata->common.lockout_policy = LockoutPolicy::kAttemptLimited;
    }
  } else if (const auto* recovery_metadata =
                 std::get_if<auth_factor::SerializedCryptohomeRecoveryMetadata>(
                     &auth_factor_table->metadata)) {
    if (!ConvertCryptohomeRecoveryMetadataFromFlatbuffer(*recovery_metadata,
                                                         metadata)) {
      LOG(ERROR) << "Failed to convert SerializedAuthFactor recovery metadata";
      return false;
    }
  } else if (const auto* smart_card_metadata =
                 std::get_if<auth_factor::SerializedSmartCardMetadata>(
                     &auth_factor_table->metadata)) {
    if (!ConvertSmartCardMetadataFromFlatbuffer(*smart_card_metadata,
                                                metadata)) {
      LOG(ERROR)
          << "Failed to convert SerializedAuthFactor smart card metadata";
      return false;
    }
  } else if (const auto* kiosk_metadata =
                 std::get_if<auth_factor::SerializedKioskMetadata>(
                     &auth_factor_table->metadata)) {
    if (!ConvertKioskMetadataFromFlatbuffer(*kiosk_metadata, metadata)) {
      LOG(ERROR) << "Failed to convert SerializedAuthFactor kiosk metadata";
      return false;
    }
  } else if (const auto* fingerprint_metadata =
                 std::get_if<auth_factor::SerializedFingerprintMetadata>(
                     &auth_factor_table->metadata)) {
    if (!ConvertFingerprintMetadataFromFlatbuffer(*fingerprint_metadata,
                                                  metadata)) {
      LOG(ERROR)
          << "Failed to convert SerializedAuthFactor fingerprint metadata";
      return false;
    }
  } else {
    LOG(ERROR) << "SerializedAuthFactor has unknown metadata";
    return false;
  }

  if (!metadata->common.lockout_policy.has_value()) {
    metadata->common.lockout_policy = LockoutPolicy::kNoLockout;
  }
  return true;
}

}  // namespace

AuthFactorManager::AuthFactorManager(Platform* platform) : platform_(platform) {
  DCHECK(platform_);
}

AuthFactorManager::~AuthFactorManager() = default;

CryptohomeStatus AuthFactorManager::SaveAuthFactor(
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
  std::optional<brillo::SecureBlob> flatbuffer =
      SerializeAuthFactor(auth_factor);

  if (!flatbuffer.has_value()) {
    LOG(ERROR) << "Failed to serialize auth factor " << auth_factor.label()
               << " of type " << AuthFactorTypeToString(auth_factor.type());
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerSerializeFailedInSave),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  brillo::Blob auth_factor_to_save(flatbuffer.value().begin(),
                                   flatbuffer.value().end());
  // Write the file.
  if (!platform_->WriteFileAtomicDurable(file_path.value(), auth_factor_to_save,
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

CryptohomeStatusOr<std::unique_ptr<AuthFactor>>
AuthFactorManager::LoadAuthFactor(const ObfuscatedUsername& obfuscated_username,
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

  brillo::SecureBlob auth_factor_to_read(file_contents.begin(),
                                         file_contents.end());
  AuthBlockState auth_block_state;
  AuthFactorMetadata auth_factor_metadata;
  if (!ParseAuthFactorFlatbuffer(auth_factor_to_read, &auth_block_state,
                                 &auth_factor_metadata)) {
    LOG(ERROR) << "Failed to parse persisted auth factor " << auth_factor_label
               << " of type " << AuthFactorTypeToString(auth_factor_type)
               << " for " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerParseFailedInLoad),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  return std::make_unique<AuthFactor>(auth_factor_type, auth_factor_label,
                                      auth_factor_metadata, auth_block_state);
}

std::map<std::string, std::unique_ptr<AuthFactor>>
AuthFactorManager::LoadAllAuthFactors(
    const ObfuscatedUsername& obfuscated_username) {
  std::map<std::string, std::unique_ptr<AuthFactor>> label_to_auth_factor;
  for (const auto& [label, auth_factor_type] :
       ListAuthFactors(obfuscated_username)) {
    CryptohomeStatusOr<std::unique_ptr<AuthFactor>> auth_factor =
        LoadAuthFactor(obfuscated_username, auth_factor_type, label);
    if (!auth_factor.ok()) {
      LOG(WARNING) << "Skipping malformed auth factor " << label;
      continue;
    }
    label_to_auth_factor.emplace(label, std::move(auth_factor).value());
  }
  return label_to_auth_factor;
}

AuthFactorManager::LabelToTypeMap AuthFactorManager::ListAuthFactors(
    const ObfuscatedUsername& obfuscated_username) {
  LabelToTypeMap label_to_type_map;

  std::unique_ptr<FileEnumerator> file_enumerator(platform_->GetFileEnumerator(
      AuthFactorsDirPath(obfuscated_username), /*recursive=*/false,
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
  CryptohomeStatusOr<base::FilePath> file_path = GetAuthFactorPath(
      obfuscated_username, auth_factor.type(), auth_factor.label());
  if (!file_path.ok()) {
    LOG(ERROR) << "Failed to get auth factor path in Remove.";
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerGetPathFailedInRemove),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  auth_block_utility->PrepareAuthBlockForRemoval(
      auth_factor.auth_block_state(),
      base::BindOnce(&AuthFactorManager::RemoveAuthFactorFiles,
                     base::Unretained(this), obfuscated_username, auth_factor,
                     file_path.value(), std::move(callback)));
}

void AuthFactorManager::UpdateAuthFactor(
    const ObfuscatedUsername& obfuscated_username,
    const std::string& auth_factor_label,
    AuthFactor& auth_factor,
    AuthBlockUtility* auth_block_utility,
    StatusCallback callback) {
  // 1. Load the old auth factor state from disk.
  CryptohomeStatusOr<std::unique_ptr<AuthFactor>> existing_auth_factor =
      LoadAuthFactor(obfuscated_username, auth_factor.type(),
                     auth_factor_label);
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
      SaveAuthFactor(obfuscated_username, auth_factor);
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
      existing_auth_factor.value()->auth_block_state(),
      base::BindOnce(&AuthFactorManager::LogPrepareForRemovalStatus,
                     base::Unretained(this), obfuscated_username, auth_factor,
                     std::move(callback)));
}

void AuthFactorManager::RemoveAuthFactorFiles(
    const ObfuscatedUsername& obfuscated_username,
    const AuthFactor& auth_factor,
    const base::FilePath& file_path,
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

  // Remove the file.
  if (!platform_->DeleteFileSecurely(file_path)) {
    LOG(WARNING) << "Failed to securely delete from disk auth factor "
                 << auth_factor.label() << " of type "
                 << AuthFactorTypeToString(auth_factor.type()) << " for "
                 << obfuscated_username
                 << ". Attempting to delete without zeroization.";
    if (!platform_->DeleteFile(file_path)) {
      LOG(ERROR) << "Failed to delete from disk auth factor "
                 << auth_factor.label() << " of type "
                 << AuthFactorTypeToString(auth_factor.type()) << " for "
                 << obfuscated_username;
      std::move(callback).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerDeleteFailedInRemove),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kRetry, PossibleAction::kReboot}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
      return;
    }
  }
  LOG(INFO) << "Deleted from disk auth factor label: " << auth_factor.label();

  // Remove the checksum file and only log warnings if the removal failed.
  base::FilePath auth_factor_checksum_path =
      file_path.AddExtension(kChecksumExtension);
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
  std::move(callback).Run(OkStatus<CryptohomeError>());
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
