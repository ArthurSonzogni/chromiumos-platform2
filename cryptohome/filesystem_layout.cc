// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/filesystem_layout.h"

#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/platform.h"

namespace cryptohome {

namespace {
constexpr char kShadowRoot[] = "/home/.shadow";
constexpr char kSystemSaltFile[] = "salt";
constexpr char kSkelPath[] = "/etc/skel";
constexpr char kLogicalVolumePrefix[] = "cryptohome";
constexpr char kDmcryptVolumePrefix[] = "dmcrypt";
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

base::FilePath VaultKeysetPath(const std::string& obfuscated, int index) {
  return ShadowRoot()
      .Append(obfuscated)
      .Append(kKeyFile)
      .AddExtension(base::NumberToString(index));
}

base::FilePath UserSecretStashPath(const std::string& obfuscated_username) {
  return ShadowRoot()
      .Append(obfuscated_username)
      .Append(kUserSecretStashDir)
      .Append(kUserSecretStashFile);
}

base::FilePath AuthFactorsDirPath(const std::string& obfuscated_username) {
  return ShadowRoot().Append(obfuscated_username).Append(kAuthFactorsDir);
}

base::FilePath AuthFactorPath(const std::string& obfuscated_username,
                              const std::string& auth_factor_type_string,
                              const std::string& auth_factor_label) {
  // TODO(b:208348570): check |auth_factor_label| against allowed characters.
  return ShadowRoot()
      .Append(obfuscated_username)
      .Append(kAuthFactorsDir)
      .Append(auth_factor_type_string)
      .AddExtension(auth_factor_label);
}

base::FilePath UserActivityPerIndexTimestampPath(const std::string& obfuscated,
                                                 int index) {
  return VaultKeysetPath(obfuscated, index).AddExtension(kTsFile);
}

base::FilePath UserActivityTimestampPath(const std::string& obfuscated) {
  return ShadowRoot().Append(obfuscated).Append(kTsFile);
}

base::FilePath GetEcryptfsUserVaultPath(const std::string& obfuscated) {
  return ShadowRoot().Append(obfuscated).Append(kEcryptfsVaultDir);
}

base::FilePath GetUserMountDirectory(const std::string& obfuscated_username) {
  return ShadowRoot().Append(obfuscated_username).Append(kMountDir);
}

base::FilePath GetUserTemporaryMountDirectory(
    const std::string& obfuscated_username) {
  return ShadowRoot().Append(obfuscated_username).Append(kTemporaryMountDir);
}

base::FilePath GetDmcryptUserCacheDirectory(
    const std::string& obfuscated_username) {
  return ShadowRoot().Append(obfuscated_username).Append(kDmcryptCacheDir);
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

bool InitializeFilesystemLayout(Platform* platform,
                                Crypto* crypto,
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

  if (!crypto->GetSystemSalt(salt)) {
    LOG(ERROR) << "Failed to create system salt.";
    return false;
  }
  return true;
}

}  // namespace cryptohome
