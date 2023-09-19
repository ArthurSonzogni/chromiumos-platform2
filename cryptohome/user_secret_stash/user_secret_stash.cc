// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/user_secret_stash.h"

#include <memory>
#include <optional>
#include <stdint.h>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/encrypted.h"

namespace cryptohome {
namespace {

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

CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> UserSecretStash::CreateNew(
    FileSystemKeyset file_system_keyset, brillo::SecureBlob main_key) {
  ASSIGN_OR_RETURN(DecryptedUss decrypted,
                   DecryptedUss::CreateWithMainKey(
                       std::move(file_system_keyset), std::move(main_key)));
  return base::WrapUnique(new UserSecretStash(std::move(decrypted)));
}

CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::CreateRandom(FileSystemKeyset file_system_keyset) {
  ASSIGN_OR_RETURN(
      DecryptedUss decrypted,
      DecryptedUss::CreateWithRandomMainKey(std::move(file_system_keyset)));
  return base::WrapUnique(new UserSecretStash(std::move(decrypted)));
}

CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::FromEncryptedContainer(const brillo::Blob& flatbuffer,
                                        const brillo::SecureBlob& main_key) {
  ASSIGN_OR_RETURN(DecryptedUss decrypted,
                   DecryptedUss::FromBlobUsingMainKey(flatbuffer, main_key));
  return base::WrapUnique(new UserSecretStash(std::move(decrypted)));
}

CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
UserSecretStash::FromEncryptedContainerWithWrappingKey(
    const brillo::Blob& flatbuffer,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) {
  ASSIGN_OR_RETURN(DecryptedUss decrypted,
                   DecryptedUss::FromBlobUsingWrappedKey(
                       flatbuffer, wrapping_id, wrapping_key));
  return base::WrapUnique(new UserSecretStash(std::move(decrypted)));
}

const FileSystemKeyset& UserSecretStash::GetFileSystemKeyset() const {
  return decrypted_.file_system_keyset();
}

std::optional<brillo::SecureBlob> UserSecretStash::GetResetSecretForLabel(
    const std::string& label) const {
  return decrypted_.GetResetSecret(label);
}

bool UserSecretStash::SetResetSecretForLabel(
    const std::string& label,
    const brillo::SecureBlob& secret,
    OverwriteExistingKeyBlock clobber) {
  auto transaction = decrypted_.StartTransaction();
  switch (clobber) {
    case OverwriteExistingKeyBlock::kEnabled:
      RETURN_IF_ERROR(transaction.AssignResetSecret(label, secret)).As(false);
      break;
    case OverwriteExistingKeyBlock::kDisabled:
      RETURN_IF_ERROR(transaction.InsertResetSecret(label, secret)).As(false);
      break;
  }
  return std::move(transaction).Commit().ok();
}

std::optional<brillo::SecureBlob> UserSecretStash::GetRateLimiterResetSecret(
    AuthFactorType auth_factor_type) const {
  return decrypted_.GetRateLimiterResetSecret(auth_factor_type);
}

bool UserSecretStash::SetRateLimiterResetSecret(
    AuthFactorType auth_factor_type, const brillo::SecureBlob& secret) {
  auto transaction = decrypted_.StartTransaction();
  RETURN_IF_ERROR(
      transaction.InsertRateLimiterResetSecret(auth_factor_type, secret))
      .As(false);
  return std::move(transaction).Commit().ok();
}

const std::string& UserSecretStash::GetCreatedOnOsVersion() const {
  return decrypted_.encrypted().created_on_os_version();
}

bool UserSecretStash::HasWrappedMainKey(const std::string& wrapping_id) const {
  return decrypted_.encrypted().WrappedMainKeyIds().contains(wrapping_id);
}

CryptohomeStatusOr<brillo::SecureBlob> UserSecretStash::UnwrapMainKey(
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) const {
  return decrypted_.encrypted().UnwrapMainKey(wrapping_id, wrapping_key);
}

CryptohomeStatus UserSecretStash::AddWrappedMainKey(
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key,
    OverwriteExistingKeyBlock clobber) {
  auto transaction = decrypted_.StartTransaction();
  switch (clobber) {
    case OverwriteExistingKeyBlock::kEnabled:
      RETURN_IF_ERROR(
          transaction.AssignWrappedMainKey(wrapping_id, wrapping_key));
      break;
    case OverwriteExistingKeyBlock::kDisabled:
      RETURN_IF_ERROR(
          transaction.InsertWrappedMainKey(wrapping_id, wrapping_key));
      break;
  }
  return std::move(transaction).Commit();
}

bool UserSecretStash::RenameWrappedMainKey(const std::string& old_wrapping_id,
                                           const std::string& new_wrapping_id) {
  auto transaction = decrypted_.StartTransaction();
  RETURN_IF_ERROR(
      transaction.RenameWrappedMainKey(old_wrapping_id, new_wrapping_id))
      .As(false);
  return std::move(transaction).Commit().ok();
}

bool UserSecretStash::RemoveWrappedMainKey(const std::string& wrapping_id) {
  auto transaction = decrypted_.StartTransaction();
  RETURN_IF_ERROR(transaction.RemoveWrappedMainKey(wrapping_id)).As(false);
  return std::move(transaction).Commit().ok();
}

CryptohomeStatusOr<brillo::Blob> UserSecretStash::GetEncryptedContainer() {
  return decrypted_.encrypted().ToBlob();
}

UserSecretStash::Snapshot::Snapshot(DecryptedUss decrypted)
    : decrypted_(std::move(decrypted)) {}

UserSecretStash::Snapshot UserSecretStash::TakeSnapshot() const {
  return Snapshot(decrypted_.CreateCopyForSnapshot());
}

void UserSecretStash::RestoreSnapshot(Snapshot&& snapshot) {
  decrypted_ = std::move(snapshot.decrypted_);
}

std::optional<uint64_t> UserSecretStash::GetFingerprintRateLimiterId() {
  return decrypted_.encrypted().fingerprint_rate_limiter_id();
}

bool UserSecretStash::InitializeFingerprintRateLimiterId(uint64_t id) {
  auto transaction = decrypted_.StartTransaction();
  RETURN_IF_ERROR(transaction.InitializeFingerprintRateLimiterId(id)).As(false);
  return std::move(transaction).Commit().ok();
}

UserSecretStash::UserSecretStash(DecryptedUss decrypted)
    : decrypted_(std::move(decrypted)) {}

}  // namespace cryptohome
