// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/filesystem_layout.h"

#include <string>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/platform.h"

using ::hwsec_foundation::CreateSecureRandomBlob;

namespace cryptohome {

namespace {

constexpr char kShadowRoot[] = "/home/.shadow";

constexpr char kSystemSaltFile[] = "salt";
constexpr int64_t kSystemSaltMaxSize = (1 << 20);  // 1 MB
constexpr mode_t kSaltFilePermissions = 0644;

constexpr char kSkelPath[] = "/etc/skel";
constexpr char kLogicalVolumePrefix[] = "cryptohome";
constexpr char kDmcryptVolumePrefix[] = "dmcrypt";

bool GetOrCreateSalt(Platform* platform,
                     const base::FilePath& salt_file,
                     brillo::SecureBlob* salt) {
  int64_t file_len = 0;
  if (platform->FileExists(salt_file)) {
    if (!platform->GetFileSize(salt_file, &file_len)) {
      LOG(ERROR) << "Can't get file len for " << salt_file.value();
      return false;
    }
  }
  brillo::SecureBlob local_salt;
  if (file_len == 0 || file_len > kSystemSaltMaxSize) {
    LOG(ERROR) << "Creating new salt at " << salt_file.value() << " ("
               << file_len << ")";
    // If this salt doesn't exist, automatically create it.
    local_salt = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_SALT_LENGTH);
    if (!platform->WriteSecureBlobToFileAtomicDurable(salt_file, local_salt,
                                                      kSaltFilePermissions)) {
      LOG(ERROR) << "Could not write user salt";
      return false;
    }
  } else {
    local_salt.resize(file_len);
    if (!platform->ReadFileToSecureBlob(salt_file, &local_salt)) {
      LOG(ERROR) << "Could not read salt file of length " << file_len;
      return false;
    }
  }
  if (salt) {
    salt->swap(local_salt);
  }
  return true;
}

}  // namespace

base::FilePath ShadowRoot() {
  return base::FilePath(kShadowRoot);
}

base::FilePath SystemSaltFile() {
  return ShadowRoot().Append(kSystemSaltFile);
}

base::FilePath PublicMountSaltFile() {
  return base::FilePath(kPublicMountSaltFilePath);
}

base::FilePath SkelDir() {
  return base::FilePath(kSkelPath);
}

base::FilePath UserPath(const std::string& obfuscated) {
  return ShadowRoot().Append(obfuscated);
}

base::FilePath VaultKeysetPath(const std::string& obfuscated, int index) {
  return UserPath(obfuscated)
      .Append(kKeyFile)
      .AddExtension(base::NumberToString(index));
}

base::FilePath UserSecretStashPath(const std::string& obfuscated_username) {
  return UserPath(obfuscated_username)
      .Append(kUserSecretStashDir)
      .Append(kUserSecretStashFile);
}

base::FilePath AuthFactorsDirPath(const std::string& obfuscated_username) {
  return UserPath(obfuscated_username).Append(kAuthFactorsDir);
}

base::FilePath AuthFactorPath(const std::string& obfuscated_username,
                              const std::string& auth_factor_type_string,
                              const std::string& auth_factor_label) {
  // The caller must make sure the label was sanitized.
  DCHECK(IsValidAuthFactorLabel(auth_factor_label));
  return UserPath(obfuscated_username)
      .Append(kAuthFactorsDir)
      .Append(auth_factor_type_string)
      .AddExtension(auth_factor_label);
}

base::FilePath UserActivityPerIndexTimestampPath(const std::string& obfuscated,
                                                 int index) {
  return VaultKeysetPath(obfuscated, index).AddExtension(kTsFile);
}

base::FilePath UserActivityTimestampPath(const std::string& obfuscated) {
  return UserPath(obfuscated).Append(kTsFile);
}

base::FilePath GetEcryptfsUserVaultPath(const std::string& obfuscated) {
  return UserPath(obfuscated).Append(kEcryptfsVaultDir);
}

base::FilePath GetUserMountDirectory(const std::string& obfuscated_username) {
  return UserPath(obfuscated_username).Append(kMountDir);
}

base::FilePath GetUserTemporaryMountDirectory(
    const std::string& obfuscated_username) {
  return UserPath(obfuscated_username).Append(kTemporaryMountDir);
}

base::FilePath GetDmcryptUserCacheDirectory(
    const std::string& obfuscated_username) {
  return UserPath(obfuscated_username).Append(kDmcryptCacheDir);
}

std::string LogicalVolumePrefix(const std::string& obfuscated_username) {
  return std::string(kLogicalVolumePrefix) + "-" +
         obfuscated_username.substr(0, 8) + "-";
}

std::string DmcryptVolumePrefix(const std::string& obfuscated_username) {
  return std::string(kDmcryptVolumePrefix) + "-" +
         obfuscated_username.substr(0, 8) + "-";
}

base::FilePath GetDmcryptDataVolume(const std::string& obfuscated_username) {
  return base::FilePath(kDeviceMapperDir)
      .Append(DmcryptVolumePrefix(obfuscated_username)
                  .append(kDmcryptDataContainerSuffix));
}

base::FilePath GetDmcryptCacheVolume(const std::string& obfuscated_username) {
  return base::FilePath(kDeviceMapperDir)
      .Append(DmcryptVolumePrefix(obfuscated_username)
                  .append(kDmcryptCacheContainerSuffix));
}

bool GetSystemSalt(Platform* platform, brillo::SecureBlob* salt) {
  return GetOrCreateSalt(platform, SystemSaltFile(), salt);
}

bool GetPublicMountSalt(Platform* platform, brillo::SecureBlob* salt) {
  return GetOrCreateSalt(platform, PublicMountSaltFile(), salt);
}

bool InitializeFilesystemLayout(Platform* platform,
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
