// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor_manager.h"

#include <sys/stat.h>

#include <memory>
#include <string>
#include <optional>
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

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor_generated.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state_flatbuffer.h"
#include "cryptohome/platform.h"

using brillo::Blob;
using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
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
    const std::string& obfuscated_username,
    const std::string& auth_factor_type_string,
    const std::string& auth_factor_label) {
  if (!IsValidAuthFactorLabel(auth_factor_label)) {
    LOG(ERROR) << "Invalid auth factor label " << auth_factor_label
               << " of type " << auth_factor_type_string;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocGetAuthFactorPathInvalidLabel),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  return AuthFactorPath(obfuscated_username, auth_factor_type_string,
                        auth_factor_label);
}

// Converts `auth_factor_type` to string and on success calls
// `GetAuthFactorPathFromStringType()` method above.
CryptohomeStatusOr<base::FilePath> GetAuthFactorPath(
    const std::string& obfuscated_username,
    const AuthFactorType auth_factor_type,
    const std::string& auth_factor_label) {
  const std::string type_string = AuthFactorTypeToString(auth_factor_type);
  if (type_string.empty()) {
    LOG(ERROR) << "Failed to convert auth factor type "
               << static_cast<int>(auth_factor_type) << " for factor called "
               << auth_factor_label;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocGetAuthFactorPathWrongTypeString),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  return GetAuthFactorPathFromStringType(obfuscated_username, type_string,
                                         auth_factor_label);
}

// Serializes the factor-specific metadata into the given flatbuffer builder.
// Returns the flatbuffer offset, to be used for building the outer table.
flatbuffers::Offset<SerializedCommonMetadata> SerializeCommonMetadataToOffset(
    const CommonAuthFactorMetadata& common_metadata,
    flatbuffers::FlatBufferBuilder& builder) {
  auto chromeos_offset = hwsec_foundation::ToFlatBuffer<std::string>()(
      &builder, common_metadata.chromeos_version_last_updated);
  auto chrome_offset = hwsec_foundation::ToFlatBuffer<std::string>()(
      &builder, common_metadata.chrome_version_last_updated);

  SerializedCommonMetadataBuilder cm_builder(builder);
  cm_builder.add_chromeos_version_last_updated(chromeos_offset);
  cm_builder.add_chrome_version_last_updated(chrome_offset);
  return cm_builder.Finish();
}

flatbuffers::Offset<SerializedPasswordMetadata> SerializeMetadataToOffset(
    const PasswordAuthFactorMetadata& password_metadata,
    flatbuffers::FlatBufferBuilder* builder) {
  SerializedPasswordMetadataBuilder metadata_builder(*builder);
  return metadata_builder.Finish();
}

flatbuffers::Offset<SerializedPinMetadata> SerializeMetadataToOffset(
    const PinAuthFactorMetadata& password_metadata,
    flatbuffers::FlatBufferBuilder* builder) {
  SerializedPinMetadataBuilder metadata_builder(*builder);
  return metadata_builder.Finish();
}

flatbuffers::Offset<SerializedCryptohomeRecoveryMetadata>
SerializeMetadataToOffset(
    const CryptohomeRecoveryAuthFactorMetadata& password_metadata,
    flatbuffers::FlatBufferBuilder* builder) {
  SerializedCryptohomeRecoveryMetadataBuilder metadata_builder(*builder);
  return metadata_builder.Finish();
}

flatbuffers::Offset<SerializedSmartCardMetadata> SerializeMetadataToOffset(
    const SmartCardAuthFactorMetadata& smart_card_metadata,
    flatbuffers::FlatBufferBuilder* builder) {
  auto public_key_offset = hwsec_foundation::ToFlatBuffer<brillo::Blob>()(
      builder, smart_card_metadata.public_key_spki_der);
  SerializedSmartCardMetadataBuilder metadata_builder(*builder);
  metadata_builder.add_public_key_spki_der(public_key_offset);
  return metadata_builder.Finish();
}

// Serializes the factor-specific metadata into the given flatbuffer builder.
// Returns the flatbuffer offset, to be used for building the outer table.
//
// Implemented by selecting the appropriate specific overload based on the
// factor type and delegating to it.
flatbuffers::Offset<void> SerializeMetadataToOffset(
    const AuthFactorMetadata& metadata,
    flatbuffers::FlatBufferBuilder* builder,
    SerializedAuthFactorMetadata* metadata_type) {
  if (const auto* password_metadata =
          std::get_if<PasswordAuthFactorMetadata>(&metadata.metadata)) {
    *metadata_type = SerializedAuthFactorMetadata::SerializedPasswordMetadata;
    return SerializeMetadataToOffset(*password_metadata, builder).Union();
  } else if (const auto* pin_metadata =
                 std::get_if<PinAuthFactorMetadata>(&metadata.metadata)) {
    *metadata_type = SerializedAuthFactorMetadata::SerializedPinMetadata;
    return SerializeMetadataToOffset(*pin_metadata, builder).Union();
  } else if (const auto* smart_card_metadata =
                 std::get_if<SmartCardAuthFactorMetadata>(&metadata.metadata)) {
    *metadata_type = SerializedAuthFactorMetadata::SerializedSmartCardMetadata;
    return SerializeMetadataToOffset(*smart_card_metadata, builder).Union();
  } else if (const auto* recovery_metadata =
                 std::get_if<CryptohomeRecoveryAuthFactorMetadata>(
                     &metadata.metadata)) {
    *metadata_type =
        SerializedAuthFactorMetadata::SerializedCryptohomeRecoveryMetadata;
    return SerializeMetadataToOffset(*recovery_metadata, builder).Union();
  }
  LOG(ERROR) << "Missing or unexpected auth factor metadata: "
             << metadata.metadata.index();
  return flatbuffers::Offset<void>();
}

// Serializes the auth factor into a flatbuffer blob. Returns null on failure.
std::optional<Blob> SerializeAuthFactor(const AuthFactor& auth_factor) {
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

  SerializedAuthFactorMetadata metadata_type =
      SerializedAuthFactorMetadata::NONE;
  flatbuffers::Offset<void> metadata_offset = SerializeMetadataToOffset(
      auth_factor.metadata(), &builder, &metadata_type);
  if (metadata_offset.IsNull()) {
    LOG(ERROR) << "Failed to serialize metadata";
    return std::nullopt;
  }

  auto common_metadata_offset =
      SerializeCommonMetadataToOffset(auth_factor.metadata().common, builder);

  SerializedAuthFactorBuilder auth_factor_builder(builder);
  auth_factor_builder.add_auth_block_state(auth_block_state_offset);
  auth_factor_builder.add_metadata(metadata_offset);
  auth_factor_builder.add_metadata_type(metadata_type);
  auth_factor_builder.add_common_metadata(common_metadata_offset);
  auto auth_factor_offset = auth_factor_builder.Finish();

  builder.Finish(auth_factor_offset);
  return Blob(builder.GetBufferPointer(),
              builder.GetBufferPointer() + builder.GetSize());
}

void ConvertCommonMetadataFromFlatbuffer(
    const SerializedCommonMetadata& flatbuffer_table,
    AuthFactorMetadata* metadata) {
  metadata->common = CommonAuthFactorMetadata{
      .chromeos_version_last_updated =
          hwsec_foundation::FromFlatBuffer<std::string>()(
              flatbuffer_table.chromeos_version_last_updated()),
      .chrome_version_last_updated =
          hwsec_foundation::FromFlatBuffer<std::string>()(
              flatbuffer_table.chrome_version_last_updated()),
  };
}

bool ConvertPasswordMetadataFromFlatbuffer(
    const SerializedPasswordMetadata& flatbuffer_table,
    AuthFactorMetadata* metadata) {
  // There's no password-specific metadata currently.
  metadata->metadata = PasswordAuthFactorMetadata();
  return true;
}

bool ConvertPinMetadataFromFlatbuffer(
    const SerializedPinMetadata& flatbuffer_table,
    AuthFactorMetadata* metadata) {
  // There's no pin-specific metadata currently.
  metadata->metadata = PinAuthFactorMetadata();
  return true;
}

bool ConvertCryptohomeRecoveryMetadataFromFlatbuffer(
    const SerializedCryptohomeRecoveryMetadata& flatbuffer_table,
    AuthFactorMetadata* metadata) {
  // There's no metadata currently.
  metadata->metadata = CryptohomeRecoveryAuthFactorMetadata();
  return true;
}

bool ConvertSmartCardMetadataFromFlatbuffer(
    const SerializedSmartCardMetadata& flatbuffer_table,
    AuthFactorMetadata* metadata) {
  auto data = hwsec_foundation::FromFlatBuffer<brillo::Blob>()(
      flatbuffer_table.public_key_spki_der());
  metadata->metadata = SmartCardAuthFactorMetadata{.public_key_spki_der = data};
  return true;
}

bool ParseAuthFactorFlatbuffer(const Blob& flatbuffer,
                               AuthBlockState* auth_block_state,
                               AuthFactorMetadata* metadata) {
  flatbuffers::Verifier flatbuffer_verifier(flatbuffer.data(),
                                            flatbuffer.size());
  if (!VerifySerializedAuthFactorBuffer(flatbuffer_verifier)) {
    LOG(ERROR) << "The SerializedAuthFactor flatbuffer is invalid";
    return false;
  }

  auto auth_factor_table = GetSerializedAuthFactor(flatbuffer.data());

  // Extract the auth block state from the serialized data.
  if (!auth_factor_table->auth_block_state()) {
    LOG(ERROR) << "SerializedAuthFactor has no auth block state";
    return false;
  }
  *auth_block_state = hwsec_foundation::FromFlatBuffer<AuthBlockState>()(
      auth_factor_table->auth_block_state());

  // Extract the common metadata from the serialized data.
  if (!auth_factor_table->common_metadata()) {
    // This is not an error, old auth factors have no common metadata.
    LOG(WARNING) << "SerializedAuthFactor has no common metadata";
  } else {
    ConvertCommonMetadataFromFlatbuffer(*auth_factor_table->common_metadata(),
                                        metadata);
  }

  // Extract the factor-specific metadata from the serialized data.
  if (!auth_factor_table->metadata()) {
    LOG(ERROR) << "SerializedAuthFactor has no metadata";
    return false;
  }
  if (const SerializedPasswordMetadata* password_metadata =
          auth_factor_table->metadata_as_SerializedPasswordMetadata()) {
    if (!ConvertPasswordMetadataFromFlatbuffer(*password_metadata, metadata)) {
      LOG(ERROR) << "Failed to convert SerializedAuthFactor password metadata";
      return false;
    }
  } else if (const SerializedPinMetadata* pin_metadata =
                 auth_factor_table->metadata_as_SerializedPinMetadata()) {
    if (!ConvertPinMetadataFromFlatbuffer(*pin_metadata, metadata)) {
      LOG(ERROR) << "Failed to convert SerializedAuthFactor pin metadata";
      return false;
    }
  } else if (const SerializedCryptohomeRecoveryMetadata* recovery_metadata =
                 auth_factor_table
                     ->metadata_as_SerializedCryptohomeRecoveryMetadata()) {
    if (!ConvertCryptohomeRecoveryMetadataFromFlatbuffer(*recovery_metadata,
                                                         metadata)) {
      LOG(ERROR) << "Failed to convert SerializedAuthFactor recovery metadata";
      return false;
    }
  } else if (const SerializedSmartCardMetadata* smart_card_metadata =
                 auth_factor_table->metadata_as_SerializedSmartCardMetadata()) {
    if (!ConvertSmartCardMetadataFromFlatbuffer(*smart_card_metadata,
                                                metadata)) {
      LOG(ERROR)
          << "Failed to convert SerializedAuthFactor smart card metadata";
      return false;
    }
  } else {
    LOG(ERROR) << "SerializedAuthFactor has unknown metadata";
    return false;
  }

  return true;
}

}  // namespace

AuthFactorManager::AuthFactorManager(Platform* platform) : platform_(platform) {
  DCHECK(platform_);
}

AuthFactorManager::~AuthFactorManager() = default;

CryptohomeStatus AuthFactorManager::SaveAuthFactor(
    const std::string& obfuscated_username, const AuthFactor& auth_factor) {
  CryptohomeStatusOr<base::FilePath> file_path = GetAuthFactorPath(
      obfuscated_username, auth_factor.type(), auth_factor.label());
  if (!file_path.ok()) {
    LOG(ERROR) << "Failed to get auth factor path in Save.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerGetPathFailedInSave),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  // Create a flatbuffer to be persisted.
  std::optional<Blob> flatbuffer = SerializeAuthFactor(auth_factor);
  if (!flatbuffer.has_value()) {
    LOG(ERROR) << "Failed to serialize auth factor " << auth_factor.label()
               << " of type " << AuthFactorTypeToString(auth_factor.type());
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerSerializeFailedInSave),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  // Write the file.
  if (!platform_->WriteFileAtomicDurable(file_path.value(), flatbuffer.value(),
                                         kAuthFactorFilePermissions)) {
    LOG(ERROR) << "Failed to persist auth factor " << auth_factor.label()
               << " of type " << AuthFactorTypeToString(auth_factor.type())
               << " for " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerWriteFailedInSave),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  return OkStatus<CryptohomeError>();
}

CryptohomeStatusOr<std::unique_ptr<AuthFactor>>
AuthFactorManager::LoadAuthFactor(const std::string& obfuscated_username,
                                  AuthFactorType auth_factor_type,
                                  const std::string& auth_factor_label) {
  CryptohomeStatusOr<base::FilePath> file_path = GetAuthFactorPath(
      obfuscated_username, auth_factor_type, auth_factor_label);
  if (!file_path.ok()) {
    LOG(ERROR) << "Failed to get auth factor path in Load.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerGetPathFailedInLoad),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  Blob file_contents;
  if (!platform_->ReadFile(file_path.value(), &file_contents)) {
    LOG(ERROR) << "Failed to load persisted auth factor " << auth_factor_label
               << " of type " << AuthFactorTypeToString(auth_factor_type)
               << " for " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerReadFailedInLoad),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  AuthBlockState auth_block_state;
  AuthFactorMetadata auth_factor_metadata;
  if (!ParseAuthFactorFlatbuffer(file_contents, &auth_block_state,
                                 &auth_factor_metadata)) {
    LOG(ERROR) << "Failed to parse persisted auth factor " << auth_factor_label
               << " of type " << AuthFactorTypeToString(auth_factor_type)
               << " for " << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerParseFailedInLoad),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  return std::make_unique<AuthFactor>(auth_factor_type, auth_factor_label,
                                      auth_factor_metadata, auth_block_state);
}

AuthFactorManager::LabelToTypeMap AuthFactorManager::ListAuthFactors(
    const std::string& obfuscated_username) {
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

CryptohomeStatus AuthFactorManager::RemoveAuthFactor(
    const std::string& obfuscated_username,
    const AuthFactor& auth_factor,
    AuthBlockUtility* auth_block_utility) {
  CryptohomeStatusOr<base::FilePath> file_path = GetAuthFactorPath(
      obfuscated_username, auth_factor.type(), auth_factor.label());
  if (!file_path.ok()) {
    LOG(ERROR) << "Failed to get auth factor path in Remove.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerGetPathFailedInRemove),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  CryptoStatus crypto_status =
      auth_factor.PrepareForRemoval(auth_block_utility);
  if (!crypto_status.ok()) {
    LOG(WARNING) << "Failed to prepare for removal for auth factor "
                 << auth_factor.label() << " of type "
                 << AuthFactorTypeToString(auth_factor.type()) << " for "
                 << obfuscated_username;
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorManagerPrepareForRemovalFailedInRemove),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(std::move(crypto_status));
  }

  // Remove the file.
  if (!platform_->DeleteFile(file_path.value())) {
    LOG(ERROR) << "Failed to delete from disk auth factor "
               << auth_factor.label() << " of type "
               << AuthFactorTypeToString(auth_factor.type()) << " for "
               << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerDeleteFailedInRemove),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kRetry, ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  return OkStatus<CryptohomeError>();
}

CryptohomeStatus AuthFactorManager::UpdateAuthFactor(
    const std::string& obfuscated_username,
    const std::string& auth_factor_label,
    AuthFactor& auth_factor,
    AuthBlockUtility* auth_block_utility) {
  // 1. Load the old auth factor state from disk.
  CryptohomeStatusOr<std::unique_ptr<AuthFactor>> existing_auth_factor =
      LoadAuthFactor(obfuscated_username, auth_factor.type(),
                     auth_factor_label);
  if (!existing_auth_factor.ok()) {
    LOG(ERROR) << "Failed to load persisted auth factor " << auth_factor_label
               << " of type " << AuthFactorTypeToString(auth_factor.type())
               << " for " << obfuscated_username << " in Update.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerLoadFailedInUpdate),
               user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
        .Wrap(std::move(existing_auth_factor).status());
  }

  // 2. Save auth factor to disk - the old auth factor state will be overridden
  // and accessible only from `existing_auth_factor` object.
  CryptohomeStatus save_result =
      SaveAuthFactor(obfuscated_username, auth_factor);
  if (!save_result.ok()) {
    LOG(ERROR) << "Failed to save auth factor " << auth_factor.label()
               << " of type " << AuthFactorTypeToString(auth_factor.type())
               << " for " << obfuscated_username << " in Update.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthFactorManagerSaveFailedInUpdate),
               user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
        .Wrap(std::move(save_result));
  }

  // 3. The old auth factor state was removed from disk. Call
  // `PrepareForRemoval()` to complete the removal.
  CryptoStatus crypto_status =
      existing_auth_factor.value()->PrepareForRemoval(auth_block_utility);
  if (!crypto_status.ok()) {
    LOG(WARNING) << "PrepareForRemoval failed for auth factor "
                 << auth_factor.label() << " of type "
                 << AuthFactorTypeToString(auth_factor.type()) << " for "
                 << obfuscated_username << " in Update.";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorManagerPrepareForRemovalFailedInUpdate),
               user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT)
        .Wrap(std::move(crypto_status));
  }

  return OkStatus<CryptohomeError>();
}

}  // namespace cryptohome
