// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/user_secret_stash.h"

#include <map>
#include <memory>
#include <optional>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <base/system/sys_info.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_container.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_payload.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/encrypted.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::AesGcmEncrypt;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

constexpr char kEnableUssFeatureTestFlagName[] = "uss_enabled";
constexpr char kDisableUssFeatureTestFlagName[] = "uss_disabled";

std::optional<bool>& GetUserSecretStashExperimentOverride() {
  // The static variable holding the overridden state. The default state is
  // nullopt, which fallbacks to checking whether flag file exists.
  static std::optional<bool> uss_experiment_enabled;
  return uss_experiment_enabled;
}

bool EnableUssFeatureTestFlagFileExists(Platform* platform) {
  return DoesFlagFileExist(kEnableUssFeatureTestFlagName, platform);
}

bool DisableUssFeatureTestFlagFileExists(Platform* platform) {
  return DoesFlagFileExist(kDisableUssFeatureTestFlagName, platform);
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

// Returns the UserSecretStash experiment flag value.
UssExperimentFlag UserSecretStashExperimentResult(Platform* platform) {
  // 1. If the state is overridden by unit tests, return this value.
  if (GetUserSecretStashExperimentOverride().has_value()) {
    return GetUserSecretStashExperimentOverride().value()
               ? UssExperimentFlag::kEnabled
               : UssExperimentFlag::kDisabled;
  }
  // 2. If no unittest override defer to checking the feature test file
  // existence. The disable file precedes the enable file.
  if (DisableUssFeatureTestFlagFileExists(platform)) {
    return UssExperimentFlag::kDisabled;
  }
  if (EnableUssFeatureTestFlagFileExists(platform)) {
    return UssExperimentFlag::kEnabled;
  }
  // 3. Without overrides, the behavior is to always enable UserSecretStash
  // experiment.
  return UssExperimentFlag::kEnabled;
}

}  // namespace

bool IsUserSecretStashExperimentEnabled(Platform* platform) {
  return UserSecretStashExperimentResult(platform) ==
         UssExperimentFlag::kEnabled;
}

void ResetUserSecretStashExperimentForTesting() {
  GetUserSecretStashExperimentOverride().reset();
}

std::optional<bool> SetUserSecretStashExperimentForTesting(
    std::optional<bool> enabled) {
  std::optional<bool> original = GetUserSecretStashExperimentOverride();
  GetUserSecretStashExperimentOverride() = enabled;
  return original;
}

// static
CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::CreateRandom(FileSystemKeyset file_system_keyset) {
  std::string current_os_version = GetCurrentOsVersion();

  return base::WrapUnique(new UserSecretStash(std::move(file_system_keyset),
                                              std::move(current_os_version)));
}

// static
CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::FromEncryptedContainer(const brillo::Blob& flatbuffer,
                                        const brillo::SecureBlob& main_key) {
  ASSIGN_OR_RETURN(DecryptedUss decrypted,
                   DecryptedUss::FromBlobUsingMainKey(flatbuffer, main_key));
  return base::WrapUnique(new UserSecretStash(std::move(decrypted)));
}

// static
CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::FromEncryptedContainerWithWrappingKey(
    const brillo::Blob& flatbuffer,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key,
    brillo::SecureBlob* main_key) {
  ASSIGN_OR_RETURN(auto decrypted_and_main_key,
                   DecryptedUss::FromBlobUsingWrappedKey(
                       flatbuffer, wrapping_id, wrapping_key));
  auto& [decrypted, unwrapped_main_key] = decrypted_and_main_key;
  *main_key = std::move(unwrapped_main_key);
  return base::WrapUnique(new UserSecretStash(std::move(decrypted)));
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

std::optional<brillo::SecureBlob> UserSecretStash::GetRateLimiterResetSecret(
    AuthFactorType auth_factor_type) const {
  const auto iter = rate_limiter_reset_secrets_.find(auth_factor_type);
  if (iter == rate_limiter_reset_secrets_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

bool UserSecretStash::SetRateLimiterResetSecret(
    AuthFactorType auth_factor_type, const brillo::SecureBlob& secret) {
  const auto result =
      rate_limiter_reset_secrets_.insert({auth_factor_type, secret});
  return result.second;
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
  EncryptedUss encrypted({.wrapped_key_blocks = wrapped_key_blocks_});
  CryptohomeStatusOr<brillo::SecureBlob> main_key =
      encrypted.UnwrapMainKey(wrapping_id, wrapping_key);
  if (!main_key.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocUSSUnwrapMKFailedInUnwrapMK))
        .Wrap(std::move(main_key).err_status());
  }
  return *main_key;
}

CryptohomeStatus UserSecretStash::AddWrappedMainKey(
    const brillo::SecureBlob& main_key,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key,
    OverwriteExistingKeyBlock clobber) {
  // Verify preconditions.
  if (main_key.empty()) {
    NOTREACHED() << "Empty UserSecretStash main key is passed for wrapping.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSMainKeyEmptyInAddWrappedMainKey),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  if (wrapping_id.empty()) {
    NOTREACHED()
        << "Empty wrapping ID is passed for UserSecretStash main key wrapping.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSWrappingIDEmptyInAddWrappedMainKey),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  if (wrapping_key.size() != kAesGcm256KeySize) {
    NOTREACHED() << "Wrong wrapping key size is passed for UserSecretStash "
                    "main key wrapping. Received: "
                 << wrapping_key.size() << ", expected " << kAesGcm256KeySize
                 << ".";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSWrappingWrongSizeInAddWrappedMainKey),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  // Protect from duplicate wrapping IDs if clobbering is not enabled.
  if (wrapped_key_blocks_.count(wrapping_id) &&
      !(clobber == OverwriteExistingKeyBlock::kEnabled)) {
    LOG(ERROR) << "A UserSecretStash main key with the given wrapping_id "
                  "already exists.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSDuplicateWrappingInAddWrappedMainKey),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kAuth, PossibleAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  // Perform the wrapping.
  brillo::Blob iv, gcm_tag, encrypted_key;
  if (!AesGcmEncrypt(main_key, /*ad=*/std::nullopt, wrapping_key, &iv, &gcm_tag,
                     &encrypted_key)) {
    LOG(ERROR) << "Failed to wrap UserSecretStash main key.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSEncryptFailedInAddWrappedMainKey),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  wrapped_key_blocks_[wrapping_id] = EncryptedUss::WrappedKeyBlock{
      .encryption_algorithm = UserSecretStashEncryptionAlgorithm::AES_GCM_256,
      .encrypted_key = encrypted_key,
      .iv = iv,
      .gcm_tag = gcm_tag,
  };
  return OkStatus<CryptohomeError>();
}

bool UserSecretStash::RenameWrappedMainKey(const std::string& old_wrapping_id,
                                           const std::string& new_wrapping_id) {
  if (wrapped_key_blocks_.contains(new_wrapping_id)) {
    return false;  // A wrapped key with the new ID already exists.
  }
  auto node = wrapped_key_blocks_.extract(old_wrapping_id);
  if (!node) {
    return false;  // No node with the old ID exists.
  }
  node.key() = new_wrapping_id;
  wrapped_key_blocks_.insert(std::move(node));
  return true;
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

  // Note: It can happen that the USS container is created with empty
  // |rate_limiter_reset_secrets_| if no PinWeaver credentials are present yet.
  for (const auto& item : rate_limiter_reset_secrets_) {
    AuthFactorType auth_factor_type = item.first;
    const brillo::SecureBlob& reset_secret = item.second;
    payload.rate_limiter_reset_secrets.push_back(TypeToResetSecretMapping{
        .auth_factor_type = static_cast<unsigned int>(auth_factor_type),
        .reset_secret = reset_secret,
    });
  }

  std::optional<brillo::SecureBlob> serialized_payload = payload.Serialize();
  if (!serialized_payload.has_value()) {
    LOG(ERROR) << "Failed to serialize UserSecretStashPayload";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSPayloadSerializeFailedInGetEncContainer),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kAuth, PossibleAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  brillo::Blob gcm_tag, iv, ciphertext;
  if (!AesGcmEncrypt(serialized_payload.value(), /*ad=*/std::nullopt, main_key,
                     &iv, &gcm_tag, &ciphertext)) {
    LOG(ERROR) << "Failed to encrypt UserSecretStash";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSPayloadEncryptFailedInGetEncContainer),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kAuth, PossibleAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  UserSecretStashContainer container = {
      .encryption_algorithm = UserSecretStashEncryptionAlgorithm::AES_GCM_256,
      .ciphertext = ciphertext,
      .iv = iv,
      .gcm_tag = gcm_tag,
      .created_on_os_version = created_on_os_version_,
      .user_metadata = user_metadata_,
  };
  // Note: It can happen that the USS container is created with empty
  // |wrapped_key_blocks_| - they may be added later, when the user registers
  // the first credential with their cryptohome.
  for (const auto& item : wrapped_key_blocks_) {
    const std::string& wrapping_id = item.first;
    const EncryptedUss::WrappedKeyBlock& wrapped_key_block = item.second;
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
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kAuth, PossibleAction::kDeleteVault}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return serialized_contaner.value();
}

UserSecretStash::Snapshot::Snapshot(
    const std::map<std::string, EncryptedUss::WrappedKeyBlock>&
        wrapped_key_blocks,
    const std::map<std::string, brillo::SecureBlob>& reset_secrets,
    const std::map<AuthFactorType, brillo::SecureBlob>&
        rate_limiter_reset_secrets,
    const UserMetadata& user_metadata)
    : wrapped_key_blocks_(wrapped_key_blocks),
      reset_secrets_(reset_secrets),
      rate_limiter_reset_secrets_(rate_limiter_reset_secrets),
      user_metadata_(user_metadata) {}

UserSecretStash::Snapshot UserSecretStash::TakeSnapshot() const {
  return Snapshot(wrapped_key_blocks_, reset_secrets_,
                  rate_limiter_reset_secrets_, user_metadata_);
}

void UserSecretStash::RestoreSnapshot(Snapshot&& snapshot) {
  wrapped_key_blocks_ = std::move(snapshot.wrapped_key_blocks_);
  reset_secrets_ = std::move(snapshot.reset_secrets_);
  rate_limiter_reset_secrets_ = std::move(snapshot.rate_limiter_reset_secrets_);
  user_metadata_ = std::move(snapshot.user_metadata_);
}

std::optional<uint64_t> UserSecretStash::GetFingerprintRateLimiterId() {
  return user_metadata_.fingerprint_rate_limiter_id;
}

bool UserSecretStash::InitializeFingerprintRateLimiterId(uint64_t id) {
  if (user_metadata_.fingerprint_rate_limiter_id.has_value()) {
    return false;
  }
  user_metadata_.fingerprint_rate_limiter_id = id;
  return true;
}

UserSecretStash::UserSecretStash(DecryptedUss decrypted) {
  std::move(decrypted).ExtractContents(
      file_system_keyset_, wrapped_key_blocks_, created_on_os_version_,
      reset_secrets_, rate_limiter_reset_secrets_, user_metadata_);
}

UserSecretStash::UserSecretStash(FileSystemKeyset file_system_keyset,
                                 std::string created_on_os_version)
    : file_system_keyset_(std::move(file_system_keyset)),
      created_on_os_version_(std::move(created_on_os_version)) {}

}  // namespace cryptohome
