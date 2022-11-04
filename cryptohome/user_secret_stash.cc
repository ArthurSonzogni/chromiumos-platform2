// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/system/sys_info.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include <map>
#include <memory>
#include <optional>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_container.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_payload.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/file_system_keyset.h"

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorAction;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec_foundation::AesGcmDecrypt;
using ::hwsec_foundation::AesGcmEncrypt;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::kAesGcmIVSize;
using ::hwsec_foundation::kAesGcmTagSize;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

namespace cryptohome {

namespace {

// The current experiment version of the USS implementation. When there is a
// critical bug in USS such that we need to stop and rollback the experiment,
// this version will be marked as invalid in the server side. After the bug is
// fixed and USS can be enabled again, this version needs to be incremented.
constexpr int kCurrentUssVersion = 1;

constexpr char kEnableUssFeatureTestFlagPath[] =
    "/var/lib/cryptohome/uss_enabled";
constexpr char kDisableUssFeatureTestFlagPath[] =
    "/var/lib/cryptohome/uss_disabled";
constexpr char kEnableUssFlagPath[] =
    "/var/lib/cryptohome/uss_enabled_until_next_update";
constexpr char kDisableUssFlagPath[] =
    "/var/lib/cryptohome/uss_disabled_until_next_update";

std::optional<bool>& GetUserSecretStashExperimentFlag() {
  // The static variable holding the overridden state. The default state is
  // nullopt, which fallbacks to the default enabled/disabled state.
  static std::optional<bool> uss_experiment_enabled;
  return uss_experiment_enabled;
}

std::optional<bool>& GetUserSecretStashExperimentOverride() {
  // The static variable holding the overridden state. The default state is
  // nullopt, which fallbacks to checking whether flag file exists.
  static std::optional<bool> uss_experiment_enabled;
  return uss_experiment_enabled;
}

bool EnableUSSFeatureTestFlagFileExists() {
  return base::PathExists(base::FilePath(kEnableUssFeatureTestFlagPath));
}

bool DisableUSSFeatureTestFlagFileExists() {
  return base::PathExists(base::FilePath(kDisableUssFeatureTestFlagPath));
}

bool EnableUSSFlagFileExists() {
  return base::PathExists(base::FilePath(kEnableUssFlagPath));
}

bool DisableUSSFlagFileExists() {
  return base::PathExists(base::FilePath(kDisableUssFlagPath));
}

void UpdateUSSStatusOnDisk(Platform* platform, UssExperimentFlag flag) {
  switch (flag) {
    case UssExperimentFlag::kDisabled:
      platform->TouchFileDurable(base::FilePath(kDisableUssFlagPath));
      platform->DeleteFileDurable(base::FilePath(kEnableUssFlagPath));
      return;
    case UssExperimentFlag::kEnabled:
      platform->TouchFileDurable(base::FilePath(kEnableUssFlagPath));
      platform->DeleteFileDurable(base::FilePath(kDisableUssFlagPath));
      return;
    case UssExperimentFlag::kNotFound:
    case UssExperimentFlag::kMaxValue:
      return;
  }
}

// Loads the current OS version from the CHROMEOS_RELEASE_VERSION field in
// /etc/lsb-release. Returns an empty string on failure.
std::string GetCurrentOsVersion() {
  std::string version;
  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_VERSION",
                                         &version)) {
    return std::string();
  }
  return version;
}

// Extracts the file system keyset from the given USS payload. Returns nullopt
// on failure.
CryptohomeStatusOr<FileSystemKeyset> GetFileSystemKeyFromPayload(
    const UserSecretStashPayload& uss_payload) {
  if (uss_payload.fek.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFEKInGetFSKeyFromPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (uss_payload.fnek.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFNEKInGetFSKeyFromPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (uss_payload.fek_salt.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK salt";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFEKSaltInGetFSKeyFromPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (uss_payload.fnek_salt.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK salt";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFNEKSaltInGetFSKeyFromPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (uss_payload.fek_sig.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK signature";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFEKSigInGetFSKeyFromPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (uss_payload.fnek_sig.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK signature";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoFNEKSigInGetFSKeyFromPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (uss_payload.chaps_key.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no Chaps key";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoChapsKeyInGetFSKeyFromPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  FileSystemKey file_system_key = {
      .fek = uss_payload.fek,
      .fnek = uss_payload.fnek,
      .fek_salt = uss_payload.fek_salt,
      .fnek_salt = uss_payload.fnek_salt,
  };
  FileSystemKeyReference file_system_key_reference = {
      .fek_sig = uss_payload.fek_sig,
      .fnek_sig = uss_payload.fnek_sig,
  };
  return FileSystemKeyset(std::move(file_system_key),
                          std::move(file_system_key_reference),
                          uss_payload.chaps_key);
}

// Converts the wrapped key block information from serializable structs
// (autogenerated by the Python script) into the mapping from wrapping_id to
// `UserSecretStash::WrappedKeyBlock`.
// Malformed and duplicate entries are logged and skipped.
std::map<std::string, UserSecretStash::WrappedKeyBlock>
GetKeyBlocksFromSerializableStructs(
    const std::vector<UserSecretStashWrappedKeyBlock>& serializable_blocks) {
  std::map<std::string, UserSecretStash::WrappedKeyBlock> key_blocks;

  for (const UserSecretStashWrappedKeyBlock& serializable_block :
       serializable_blocks) {
    if (serializable_block.wrapping_id.empty()) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with an empty ID.";
      continue;
    }
    if (key_blocks.count(serializable_block.wrapping_id)) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with duplicate ID "
          << serializable_block.wrapping_id << ".";
      continue;
    }

    if (!serializable_block.encryption_algorithm.has_value()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "unset algorithm";
      continue;
    }
    if (serializable_block.encryption_algorithm.value() !=
        UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "unknown algorithm: "
                   << static_cast<int>(
                          serializable_block.encryption_algorithm.value());
      continue;
    }

    if (serializable_block.encrypted_key.empty()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "empty encrypted key.";
      continue;
    }

    if (serializable_block.iv.empty()) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with an empty IV.";
      continue;
    }

    if (serializable_block.gcm_tag.empty()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "empty AES-GCM tag.";
      continue;
    }

    UserSecretStash::WrappedKeyBlock key_block = {
        .encryption_algorithm = serializable_block.encryption_algorithm.value(),
        .encrypted_key = serializable_block.encrypted_key,
        .iv = serializable_block.iv,
        .gcm_tag = serializable_block.gcm_tag,
    };
    key_blocks.insert({serializable_block.wrapping_id, std::move(key_block)});
  }

  return key_blocks;
}

// Parses the USS container flatbuffer. On success, populates `ciphertext`,
// `iv`, `tag`, `wrapped_key_blocks`, `created_on_os_version`; on failure,
// returns false.
CryptohomeStatus GetContainerFromFlatbuffer(
    const brillo::Blob& flatbuffer,
    brillo::Blob* ciphertext,
    brillo::Blob* iv,
    brillo::Blob* tag,
    std::map<std::string, UserSecretStash::WrappedKeyBlock>* wrapped_key_blocks,
    std::string* created_on_os_version) {
  std::optional<UserSecretStashContainer> deserialized =
      UserSecretStashContainer::Deserialize(flatbuffer);
  if (!deserialized.has_value()) {
    LOG(ERROR) << "Failed to deserialize UserSecretStashContainer";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSDeserializeFailedInGetContainerFromFB),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  if (!deserialized.value().encryption_algorithm.has_value()) {
    LOG(ERROR) << "UserSecretStashContainer has no algorithm set";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoAlgInGetContainerFromFB),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (deserialized.value().encryption_algorithm.value() !=
      UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
    LOG(ERROR) << "UserSecretStashContainer uses unknown algorithm: "
               << static_cast<int>(deserialized->encryption_algorithm.value());
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSUnknownAlgInGetContainerFromFB),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  if (deserialized.value().ciphertext.empty()) {
    LOG(ERROR) << "UserSecretStash has empty ciphertext";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoCiphertextInGetContainerFromFB),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  *ciphertext = deserialized.value().ciphertext;

  if (deserialized.value().iv.empty()) {
    LOG(ERROR) << "UserSecretStash has empty IV";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoIVInGetContainerFromFB),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (deserialized.value().iv.size() != kAesGcmIVSize) {
    LOG(ERROR) << "UserSecretStash has IV of wrong length: "
               << deserialized.value().iv.size()
               << ", expected: " << kAesGcmIVSize;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSIVWrongSizeInGetContainerFromFB),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  *iv = deserialized.value().iv;

  if (deserialized.value().gcm_tag.empty()) {
    LOG(ERROR) << "UserSecretStash has empty AES-GCM tag";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoGCMTagInGetContainerFromFB),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (deserialized.value().gcm_tag.size() != kAesGcmTagSize) {
    LOG(ERROR) << "UserSecretStash has AES-GCM tag of wrong length: "
               << deserialized.value().gcm_tag.size()
               << ", expected: " << kAesGcmTagSize;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSTagWrongSizeInGetContainerFromFB),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  *tag = deserialized.value().gcm_tag;

  *wrapped_key_blocks = GetKeyBlocksFromSerializableStructs(
      deserialized.value().wrapped_key_blocks);

  *created_on_os_version = deserialized.value().created_on_os_version;

  return OkStatus<CryptohomeError>();
}

CryptohomeStatusOr<brillo::SecureBlob> UnwrapMainKeyFromBlocks(
    const std::map<std::string, UserSecretStash::WrappedKeyBlock>&
        wrapped_key_blocks,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) {
  // Verify preconditions.
  if (wrapping_id.empty()) {
    NOTREACHED() << "Empty wrapping ID is passed for UserSecretStash main key "
                    "unwrapping.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSEmptyWrappingIDInUnwrapMKFromBlocks),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (wrapping_key.size() != kAesGcm256KeySize) {
    NOTREACHED() << "Wrong wrapping key size is passed for UserSecretStash "
                    "main key unwrapping. Received: "
                 << wrapping_key.size() << ", expected " << kAesGcm256KeySize
                 << ".";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSWrongWKSizeInUnwrapMKFromBlocks),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  // Find the wrapped key block.
  const auto wrapped_key_block_iter = wrapped_key_blocks.find(wrapping_id);
  if (wrapped_key_block_iter == wrapped_key_blocks.end()) {
    LOG(ERROR)
        << "UserSecretStash wrapped key block with the given ID not found.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSWrappedBlockNotFoundInUnwrapMKFromBlocks),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  const UserSecretStash::WrappedKeyBlock& wrapped_key_block =
      wrapped_key_block_iter->second;

  // Verify the wrapped key block format. No NOTREACHED() checks here, since the
  // key block is a deserialization of the persisted blob.
  if (wrapped_key_block.encryption_algorithm !=
      UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
    LOG(ERROR) << "UserSecretStash wrapped main key uses unknown algorithm: "
               << static_cast<int>(wrapped_key_block.encryption_algorithm)
               << ".";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSUnknownAlgInUnwrapMKFromBlocks),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (wrapped_key_block.encrypted_key.empty()) {
    LOG(ERROR) << "UserSecretStash wrapped main key has empty encrypted key.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSEmptyEncKeyInUnwrapMKFromBlocks),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (wrapped_key_block.iv.size() != kAesGcmIVSize) {
    LOG(ERROR) << "UserSecretStash wrapped main key has IV of wrong length: "
               << wrapped_key_block.iv.size() << ", expected: " << kAesGcmIVSize
               << ".";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSWrongIVSizeInUnwrapMKFromBlocks),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (wrapped_key_block.gcm_tag.size() != kAesGcmTagSize) {
    LOG(ERROR)
        << "UserSecretStash wrapped main key has AES-GCM tag of wrong length: "
        << wrapped_key_block.gcm_tag.size() << ", expected: " << kAesGcmTagSize
        << ".";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSWrongTagSizeInUnwrapMKFromBlocks),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  // Attempt the unwrapping.
  brillo::SecureBlob main_key;
  if (!AesGcmDecrypt(
          brillo::SecureBlob(wrapped_key_block.encrypted_key),
          /*ad=*/std::nullopt, brillo::SecureBlob(wrapped_key_block.gcm_tag),
          wrapping_key, brillo::SecureBlob(wrapped_key_block.iv), &main_key)) {
    LOG(ERROR) << "Failed to unwrap UserSecretStash main key";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSDecryptFailedInUnwrapMKFromBlocks),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return main_key;
}

}  // namespace

int UserSecretStashExperimentVersion() {
  return kCurrentUssVersion;
}

bool IsUserSecretStashExperimentEnabled(Platform* platform) {
  // 1. If the state is overridden by unit tests, return this value.
  if (GetUserSecretStashExperimentOverride().has_value())
    return GetUserSecretStashExperimentOverride().value();
  // 2. If no unittest override defer to checking the feature test file
  // existence. The disable file precedes the enable file.
  if (DisableUSSFeatureTestFlagFileExists()) {
    return false;
  }
  if (EnableUSSFeatureTestFlagFileExists()) {
    return true;
  }
  // 3. Check the flag set by UssExperimentConfigFetcher and persist the state
  // until next update.
  UssExperimentFlag result = UssExperimentFlag::kNotFound;
  std::optional<bool> flag = GetUserSecretStashExperimentFlag();
  if (flag.has_value()) {
    if (flag.value()) {
      result = UssExperimentFlag::kEnabled;
      UpdateUSSStatusOnDisk(platform, UssExperimentFlag::kEnabled);
    } else {
      result = UssExperimentFlag::kDisabled;
      UpdateUSSStatusOnDisk(platform, UssExperimentFlag::kDisabled);
    }
  } else {
    // When flag doesn't have any value, restore the previous value.
    // If both flag files exists, USS is disabled. If no flag file is found the
    // result will stay unchanged.
    if (EnableUSSFlagFileExists()) {
      result = UssExperimentFlag::kEnabled;
    }
    if (DisableUSSFlagFileExists()) {
      result = UssExperimentFlag::kDisabled;
    }
  }

  ReportUssExperimentFlag(result);
  return result == UssExperimentFlag::kEnabled;
}

void SetUserSecretStashExperimentFlag(bool enabled) {
  GetUserSecretStashExperimentFlag() = enabled;
}

void SetUserSecretStashExperimentForTesting(std::optional<bool> enabled) {
  GetUserSecretStashExperimentOverride() = enabled;
}

// static
CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::CreateRandom(const FileSystemKeyset& file_system_keyset) {
  std::string current_os_version = GetCurrentOsVersion();

  // Note: make_unique() wouldn't work due to the constructor being private.
  std::unique_ptr<UserSecretStash> stash(
      new UserSecretStash(file_system_keyset));
  stash->created_on_os_version_ = std::move(current_os_version);
  return stash;
}

// static
CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::FromEncryptedContainer(const brillo::Blob& flatbuffer,
                                        const brillo::SecureBlob& main_key) {
  if (main_key.size() != kAesGcm256KeySize) {
    LOG(ERROR) << "The UserSecretStash main key is of wrong length: "
               << main_key.size() << ", expected: " << kAesGcm256KeySize;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSInvalidKeySizeInFromEncContainer),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  brillo::Blob ciphertext, iv, gcm_tag;
  std::map<std::string, WrappedKeyBlock> wrapped_key_blocks;
  std::string created_on_os_version;
  CryptohomeStatus status =
      GetContainerFromFlatbuffer(flatbuffer, &ciphertext, &iv, &gcm_tag,
                                 &wrapped_key_blocks, &created_on_os_version);
  if (!status.ok()) {
    // Note: the error is already logged.
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocUSSGetFromFBFailedInFromEncContainer))
        .Wrap(std::move(status));
  }

  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> result =
      FromEncryptedPayload(ciphertext, iv, gcm_tag, wrapped_key_blocks,
                           created_on_os_version, main_key);
  if (!result.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocUSSFromPayloadFailedInFromEncContainer))
        .Wrap(std::move(result).status());
  }
  return result;
}

// static
CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::FromEncryptedPayload(
    const brillo::Blob& ciphertext,
    const brillo::Blob& iv,
    const brillo::Blob& gcm_tag,
    const std::map<std::string, WrappedKeyBlock>& wrapped_key_blocks,
    const std::string& created_on_os_version,
    const brillo::SecureBlob& main_key) {
  brillo::SecureBlob serialized_uss_payload;
  if (!AesGcmDecrypt(brillo::SecureBlob(ciphertext), /*ad=*/std::nullopt,
                     brillo::SecureBlob(gcm_tag), main_key,
                     brillo::SecureBlob(iv), &serialized_uss_payload)) {
    LOG(ERROR) << "Failed to decrypt UserSecretStash payload";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSAesGcmFailedInFromEncPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  std::optional<UserSecretStashPayload> uss_payload =
      UserSecretStashPayload::Deserialize(serialized_uss_payload);
  if (!uss_payload.has_value()) {
    LOG(ERROR) << "Failed to deserialize UserSecretStashPayload";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSDeserializeFailedInFromEncPayload),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  CryptohomeStatusOr<FileSystemKeyset> file_system_keyset_status =
      GetFileSystemKeyFromPayload(uss_payload.value());
  if (!file_system_keyset_status.ok()) {
    LOG(ERROR)
        << "UserSecretStashPayload has invalid file system keyset information";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocUSSGetFSKeyFailedInFromEncPayload))
        .Wrap(std::move(file_system_keyset_status).status());
  }

  std::map<std::string, brillo::SecureBlob> reset_secrets;
  for (const ResetSecretMapping& item : uss_payload.value().reset_secrets) {
    auto insertion_status =
        reset_secrets.insert({item.auth_factor_label, item.reset_secret});
    if (!insertion_status.second) {
      LOG(ERROR) << "UserSecretStashPayload contains multiple reset secrets "
                    "for label: "
                 << item.auth_factor_label;
    }
  }

  // Note: make_unique() wouldn't work due to the constructor being private.
  std::unique_ptr<UserSecretStash> stash(
      new UserSecretStash(file_system_keyset_status.value(), reset_secrets));
  stash->wrapped_key_blocks_ = wrapped_key_blocks;
  stash->created_on_os_version_ = created_on_os_version;
  return stash;
}

// static
CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::FromEncryptedContainerWithWrappingKey(
    const brillo::Blob& flatbuffer,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key,
    brillo::SecureBlob* main_key) {
  brillo::Blob ciphertext, iv, gcm_tag;
  std::map<std::string, WrappedKeyBlock> wrapped_key_blocks;
  std::string created_on_os_version;
  CryptohomeStatus status =
      GetContainerFromFlatbuffer(flatbuffer, &ciphertext, &iv, &gcm_tag,
                                 &wrapped_key_blocks, &created_on_os_version);
  if (!status.ok()) {
    // Note: the error is already logged.
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUSSGetFromFBFailedInFromEncContainerWithWK))
        .Wrap(std::move(status));
  }

  CryptohomeStatusOr<brillo::SecureBlob> main_key_optional =
      UnwrapMainKeyFromBlocks(wrapped_key_blocks, wrapping_id, wrapping_key);
  if (!main_key_optional.ok()) {
    // Note: the error is already logged.
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUSSUnwrapMKFailedInFromEncContainerWithWK))
        .Wrap(std::move(main_key_optional).status());
  }

  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> stash =
      FromEncryptedPayload(ciphertext, iv, gcm_tag, wrapped_key_blocks,
                           created_on_os_version, main_key_optional.value());
  if (!stash.ok()) {
    // Note: the error is already logged.
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocUSSFromPayloadFailedInFromEncContainerWithWK))
        .Wrap(std::move(stash).status());
  }
  *main_key = main_key_optional.value();
  return std::move(stash).value();
}

// static
brillo::SecureBlob UserSecretStash::CreateRandomMainKey() {
  return CreateSecureRandomBlob(kAesGcm256KeySize);
}

const FileSystemKeyset& UserSecretStash::GetFileSystemKeyset() const {
  return file_system_keyset_;
}

std::optional<brillo::SecureBlob> UserSecretStash::GetResetSecretForLabel(
    const std::string& label) const {
  const auto iter = reset_secrets_.find(label);
  if (iter == reset_secrets_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

bool UserSecretStash::SetResetSecretForLabel(const std::string& label,
                                             const brillo::SecureBlob& secret) {
  const auto result = reset_secrets_.insert({label, secret});
  return result.second;
}

bool UserSecretStash::RemoveResetSecretForLabel(const std::string& label) {
  const auto iter = reset_secrets_.find(label);
  if (iter == reset_secrets_.end()) {
    return false;
  }
  reset_secrets_.erase(iter);
  return true;
}

const std::string& UserSecretStash::GetCreatedOnOsVersion() const {
  return created_on_os_version_;
}

bool UserSecretStash::HasWrappedMainKey(const std::string& wrapping_id) const {
  return wrapped_key_blocks_.count(wrapping_id);
}

CryptohomeStatusOr<brillo::SecureBlob> UserSecretStash::UnwrapMainKey(
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) const {
  CryptohomeStatusOr<brillo::SecureBlob> result =
      UnwrapMainKeyFromBlocks(wrapped_key_blocks_, wrapping_id, wrapping_key);
  if (!result.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocUSSUnwrapMKFailedInUnwrapMK))
        .Wrap(std::move(result).status());
  }
  return result.value();
}

CryptohomeStatus UserSecretStash::AddWrappedMainKey(
    const brillo::SecureBlob& main_key,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) {
  // Verify preconditions.
  if (main_key.empty()) {
    NOTREACHED() << "Empty UserSecretStash main key is passed for wrapping.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSMainKeyEmptyInAddWrappedMainKey),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  if (wrapping_id.empty()) {
    NOTREACHED()
        << "Empty wrapping ID is passed for UserSecretStash main key wrapping.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSWrappingIDEmptyInAddWrappedMainKey),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  if (wrapping_key.size() != kAesGcm256KeySize) {
    NOTREACHED() << "Wrong wrapping key size is passed for UserSecretStash "
                    "main key wrapping. Received: "
                 << wrapping_key.size() << ", expected " << kAesGcm256KeySize
                 << ".";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSWrappingWrongSizeInAddWrappedMainKey),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  // Protect from duplicate wrapping IDs.
  if (wrapped_key_blocks_.count(wrapping_id)) {
    LOG(ERROR) << "A UserSecretStash main key with the given wrapping_id "
                  "already exists.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSDuplicateWrappingInAddWrappedMainKey),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  // Perform the wrapping.
  brillo::SecureBlob iv, gcm_tag, encrypted_key;
  if (!AesGcmEncrypt(main_key, /*ad=*/std::nullopt, wrapping_key, &iv, &gcm_tag,
                     &encrypted_key)) {
    LOG(ERROR) << "Failed to wrap UserSecretStash main key.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSEncryptFailedInAddWrappedMainKey),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  wrapped_key_blocks_[wrapping_id] = WrappedKeyBlock{
      .encryption_algorithm = UserSecretStashEncryptionAlgorithm::AES_GCM_256,
      .encrypted_key = brillo::Blob(encrypted_key.begin(), encrypted_key.end()),
      .iv = brillo::Blob(iv.begin(), iv.end()),
      .gcm_tag = brillo::Blob(gcm_tag.begin(), gcm_tag.end()),
  };
  return OkStatus<CryptohomeError>();
}

bool UserSecretStash::RemoveWrappedMainKey(const std::string& wrapping_id) {
  auto iter = wrapped_key_blocks_.find(wrapping_id);
  if (iter == wrapped_key_blocks_.end()) {
    LOG(ERROR) << "No UserSecretStash wrapped key block is found with the "
                  "given wrapping ID.";
    return false;
  }
  wrapped_key_blocks_.erase(iter);
  return true;
}

CryptohomeStatusOr<brillo::Blob> UserSecretStash::GetEncryptedContainer(
    const brillo::SecureBlob& main_key) {
  UserSecretStashPayload payload = {
      .fek = file_system_keyset_.Key().fek,
      .fnek = file_system_keyset_.Key().fnek,
      .fek_salt = file_system_keyset_.Key().fek_salt,
      .fnek_salt = file_system_keyset_.Key().fnek_salt,
      .fek_sig = file_system_keyset_.KeyReference().fek_sig,
      .fnek_sig = file_system_keyset_.KeyReference().fnek_sig,
      .chaps_key = file_system_keyset_.chaps_key(),
  };

  // Note: It can happen that the USS container is created with empty
  // |reset_secrets_| if no PinWeaver credentials are present yet.
  for (const auto& item : reset_secrets_) {
    const std::string& auth_factor_label = item.first;
    const brillo::SecureBlob& reset_secret = item.second;
    payload.reset_secrets.push_back(ResetSecretMapping{
        .auth_factor_label = auth_factor_label,
        .reset_secret = reset_secret,
    });
  }

  std::optional<brillo::SecureBlob> serialized_payload = payload.Serialize();
  if (!serialized_payload.has_value()) {
    LOG(ERROR) << "Failed to serialize UserSecretStashPayload";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSPayloadSerializeFailedInGetEncContainer),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  brillo::SecureBlob tag, iv, ciphertext;
  if (!AesGcmEncrypt(serialized_payload.value(), /*ad=*/std::nullopt, main_key,
                     &iv, &tag, &ciphertext)) {
    LOG(ERROR) << "Failed to encrypt UserSecretStash";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSPayloadEncryptFailedInGetEncContainer),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  UserSecretStashContainer container = {
      .encryption_algorithm = UserSecretStashEncryptionAlgorithm::AES_GCM_256,
      .ciphertext = brillo::Blob(ciphertext.begin(), ciphertext.end()),
      .iv = brillo::Blob(iv.begin(), iv.end()),
      .gcm_tag = brillo::Blob(tag.begin(), tag.end()),
      .created_on_os_version = created_on_os_version_,
  };
  // Note: It can happen that the USS container is created with empty
  // |wrapped_key_blocks_| - they may be added later, when the user registers
  // the first credential with their cryptohome.
  for (const auto& item : wrapped_key_blocks_) {
    const std::string& wrapping_id = item.first;
    const UserSecretStash::WrappedKeyBlock& wrapped_key_block = item.second;
    container.wrapped_key_blocks.push_back(UserSecretStashWrappedKeyBlock{
        .wrapping_id = wrapping_id,
        .encryption_algorithm = wrapped_key_block.encryption_algorithm,
        .encrypted_key = wrapped_key_block.encrypted_key,
        .iv = wrapped_key_block.iv,
        .gcm_tag = wrapped_key_block.gcm_tag,
    });
  }

  std::optional<brillo::Blob> serialized_contaner = container.Serialize();
  if (!serialized_contaner.has_value()) {
    LOG(ERROR) << "Failed to serialize UserSecretStashContainer";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSContainerSerializeFailedInGetEncContainer),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return serialized_contaner.value();
}

UserSecretStash::UserSecretStash(
    const FileSystemKeyset& file_system_keyset,
    const std::map<std::string, brillo::SecureBlob>& reset_secrets)
    : file_system_keyset_(file_system_keyset), reset_secrets_(reset_secrets) {}

UserSecretStash::UserSecretStash(const FileSystemKeyset& file_system_keyset)
    : file_system_keyset_(file_system_keyset) {}

}  // namespace cryptohome
