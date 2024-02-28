// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/filesystem_layout.h"

#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_factor/label.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/username.h"

using ::hwsec_foundation::CreateSecureRandomBlob;

namespace cryptohome {
namespace {

constexpr char kShadowRoot[] = "/home/.shadow";

constexpr char kLegacySystemSaltFile[] = "/home/.shadow/salt";
constexpr char kSystemSaltFile[] = "/var/lib/system_salt";
constexpr char kPublicMountSaltFilePath[] = "/var/lib/public_mount_salt";

constexpr int64_t kSystemSaltMaxSize = (1 << 20);  // 1 MB
constexpr mode_t kSaltFilePermissions = 0644;

constexpr char kRecoverableKeyStoreDir[] = "key_store_certs";

constexpr char kSkelPath[] = "/etc/skel";
constexpr char kLogicalVolumePrefix[] = "cryptohome";
constexpr char kDmcryptVolumePrefix[] = "dmcrypt";
constexpr char kLogicalVolumeSnapshotSuffix[] = "-rw";

// Storage for serialized RecoveryId.
constexpr char kRecoveryIdFile[] = "recovery_id";
// The path that signals the existence of a CRD connection on sign in screen.
constexpr char kRecoveryFactorLockPath[] =
    "/run/cryptohome/crd_detected_on_login_screen";

// Attempt to get an existing salt from the specified path. Returns false if the
// salt does not exist or is invalid. If the salt does exist but is invalid, it
// will also attempt to remove the file.
bool GetOrRemoveSalt(libstorage::Platform* platform,
                     const base::FilePath& salt_file,
                     brillo::SecureBlob* salt) {
  // If the file doesn't exist, fail immediately. This isn't logged as this can
  // be an expected condition.
  if (!platform->FileExists(salt_file)) {
    return false;
  }

  int64_t file_len = 0;
  if (!platform->GetFileSize(salt_file, &file_len)) {
    LOG(ERROR) << "Can't get file len for " << salt_file.value();
    return false;
  }

  if (file_len > 0 && file_len <= kSystemSaltMaxSize) {
    brillo::SecureBlob local_salt(file_len);
    if (platform->ReadFileToSecureBlob(salt_file, &local_salt)) {
      // This is the success case: the size is valid and the file is readable.
      if (salt) {
        *salt = std::move(local_salt);
      }
      return true;
    }
    LOG(ERROR) << "Could not read salt file " << salt_file << " of length "
               << file_len;
  }

  // If we get here then the file exists but is invalid or unreadable for some
  // reason. Try to remove it. If the removal fails we log it, but we return
  // false either way.
  LOG(ERROR) << "Existing salt file at " << salt_file
             << " is invalid or unreadable, attempting to delete it";
  if (!platform->DeleteFile(salt_file)) {
    LOG(ERROR) << "Salt file at " << salt_file << " could not be deleted";
  }
  return false;
}

// Attempt to get an existing salt from the specified path. If the file does not
// exist or does not contain a valid salt, this will attempt to generate a new
// salt. If that also fails, this will return false. Otherwise, it will return
// true with the salt (existing or newly created).
bool GetOrCreateSalt(libstorage::Platform* platform,
                     const base::FilePath& salt_file,
                     brillo::SecureBlob* salt) {
  int64_t file_len = 0;
  if (platform->FileExists(salt_file)) {
    if (!platform->GetFileSize(salt_file, &file_len)) {
      LOG(ERROR) << "Can't get file len for " << salt_file;
      return false;
    }
  }

  brillo::SecureBlob local_salt;
  if (file_len == 0 || file_len > kSystemSaltMaxSize) {
    LOG(INFO) << "Creating new salt at " << salt_file << " (existing length "
              << file_len << ")";
    // If this salt doesn't exist, automatically create it.
    local_salt = CreateSecureRandomBlob(kCryptohomeDefaultSaltLength);
    if (!platform->WriteSecureBlobToFileAtomicDurable(salt_file, local_salt,
                                                      kSaltFilePermissions)) {
      LOG(ERROR) << "Could not write new salt to " << salt_file;
      return false;
    }
  } else {
    local_salt.resize(file_len);
    if (!platform->ReadFileToSecureBlob(salt_file, &local_salt)) {
      LOG(ERROR) << "Could not read salt file " << salt_file << " of length "
                 << file_len;
      return false;
    }
  }
  if (salt) {
    *salt = std::move(local_salt);
  }
  return true;
}

// Get the Account ID for an AccountIdentifier proto.
Username GetAccountId(const AccountIdentifier& id) {
  if (id.has_account_id()) {
    return Username(id.account_id());
  }
  return Username(id.email());
}

}  // namespace

base::FilePath ShadowRoot() {
  return base::FilePath(kShadowRoot);
}

base::FilePath LegacySystemSaltFile() {
  return base::FilePath(kLegacySystemSaltFile);
}

base::FilePath SystemSaltFile() {
  return base::FilePath(kSystemSaltFile);
}

base::FilePath PublicMountSaltFile() {
  return base::FilePath(kPublicMountSaltFilePath);
}

base::FilePath SkelDir() {
  return base::FilePath(kSkelPath);
}

base::FilePath RecoverableKeyStoreBackendCertDir() {
  return ShadowRoot().Append(kRecoverableKeyStoreDir);
}

base::FilePath UserPath(const ObfuscatedUsername& obfuscated) {
  return ShadowRoot().Append(*obfuscated);
}

base::FilePath VaultKeysetPath(const ObfuscatedUsername& obfuscated,
                               int index) {
  return UserPath(obfuscated)
      .Append(kKeyFile)
      .AddExtension(base::NumberToString(index));
}

base::FilePath UserSecretStashPath(
    const ObfuscatedUsername& obfuscated_username, int slot) {
  CHECK_GE(slot, 0);
  return UserPath(obfuscated_username)
      .Append(kUserSecretStashDir)
      .Append(kUserSecretStashFileBase)
      .AddExtension(std::to_string(slot));
}

base::FilePath AuthFactorsDirPath(
    const ObfuscatedUsername& obfuscated_username) {
  return UserPath(obfuscated_username).Append(kAuthFactorsDir);
}

base::FilePath AuthFactorPath(const ObfuscatedUsername& obfuscated_username,
                              const std::string& auth_factor_type_string,
                              const std::string& auth_factor_label) {
  // The caller must make sure the label was sanitized.
  CHECK(IsValidAuthFactorLabel(auth_factor_label));
  return UserPath(obfuscated_username)
      .Append(kAuthFactorsDir)
      .Append(auth_factor_type_string)
      .AddExtension(auth_factor_label);
}

base::FilePath UserActivityPerIndexTimestampPath(
    const ObfuscatedUsername& obfuscated, int index) {
  return VaultKeysetPath(obfuscated, index).AddExtension(kTsFile);
}

base::FilePath UserActivityTimestampPath(const ObfuscatedUsername& obfuscated) {
  return UserPath(obfuscated).Append(kTsFile);
}

base::FilePath GetEcryptfsUserVaultPath(const ObfuscatedUsername& obfuscated) {
  return UserPath(obfuscated).Append(kEcryptfsVaultDir);
}

base::FilePath GetUserMountDirectory(
    const ObfuscatedUsername& obfuscated_username) {
  return UserPath(obfuscated_username).Append(kMountDir);
}

base::FilePath GetUserPolicyPath(
    const ObfuscatedUsername& obfuscated_username) {
  return UserPath(obfuscated_username)
      .Append(kUserPolicyDir)
      .Append(kPolicyFile);
}

base::FilePath GetUserTemporaryMountDirectory(
    const ObfuscatedUsername& obfuscated_username) {
  return UserPath(obfuscated_username).Append(kTemporaryMountDir);
}

base::FilePath GetDmcryptUserCacheDirectory(
    const ObfuscatedUsername& obfuscated_username) {
  return UserPath(obfuscated_username).Append(kDmcryptCacheDir);
}

std::string LogicalVolumePrefix(const ObfuscatedUsername& obfuscated_username) {
  return std::string(kLogicalVolumePrefix) + "-" +
         obfuscated_username->substr(0, 8) + "-";
}

std::string DmcryptVolumePrefix(const ObfuscatedUsername& obfuscated_username) {
  return std::string(kDmcryptVolumePrefix) + "-" +
         obfuscated_username->substr(0, 8) + "-";
}

base::FilePath GetDmcryptDataVolume(
    const ObfuscatedUsername& obfuscated_username) {
  return base::FilePath(kDeviceMapperDir)
      .Append(DmcryptVolumePrefix(obfuscated_username)
                  .append(kDmcryptDataContainerSuffix));
}

base::FilePath GetDmcryptCacheVolume(
    const ObfuscatedUsername& obfuscated_username) {
  return base::FilePath(kDeviceMapperDir)
      .Append(DmcryptVolumePrefix(obfuscated_username)
                  .append(kDmcryptCacheContainerSuffix));
}

base::FilePath LogicalVolumeSnapshotPath(
    const ObfuscatedUsername& obfuscated_username,
    const std::string& container_name) {
  std::string name = LogicalVolumePrefix(obfuscated_username) + container_name +
                     kLogicalVolumeSnapshotSuffix;

  return base::FilePath(kDeviceMapperDir).Append(name);
}

bool GetSystemSalt(libstorage::Platform* platform, brillo::SecureBlob* salt) {
  // Only new installations get the system salt file in the new location.
  // If the legacy salt file can be loaded, the system should keep using it.
  return GetOrRemoveSalt(platform, LegacySystemSaltFile(), salt) ||
         GetOrCreateSalt(platform, SystemSaltFile(), salt);
}

bool GetPublicMountSalt(libstorage::Platform* platform,
                        brillo::SecureBlob* salt) {
  return GetOrCreateSalt(platform, PublicMountSaltFile(), salt);
}

base::FilePath GetRecoveryIdPath(const AccountIdentifier& account_id) {
  ObfuscatedUsername obfuscated =
      brillo::cryptohome::home::SanitizeUserName(GetAccountId(account_id));
  if (obfuscated->empty()) {
    return base::FilePath();
  }
  return brillo::cryptohome::home::GetUserPath(obfuscated)
      .Append(kRecoveryIdFile);
}

base::FilePath GetRecoveryFactorLockPath() {
  return base::FilePath(kRecoveryFactorLockPath);
}

bool InitializeFilesystemLayout(libstorage::Platform* platform,
                                brillo::SecureBlob* salt) {
  const base::FilePath shadow_root = ShadowRoot();
  if (!platform->DirectoryExists(shadow_root)) {
    platform->CreateDirectory(shadow_root);
    if (platform->RestoreSELinuxContexts(shadow_root, true /*recursive*/)) {
      ReportRestoreSELinuxContextResultForShadowDir(true);
    } else {
      ReportRestoreSELinuxContextResultForShadowDir(false);
      LOG(ERROR) << "RestoreSELinuxContexts(" << shadow_root << ") failed.";
    }
  }

  if (!GetSystemSalt(platform, salt)) {
    LOG(ERROR) << "Failed to create system salt.";
    return false;
  }
  return true;
}

}  // namespace cryptohome
