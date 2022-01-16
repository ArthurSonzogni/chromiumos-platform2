// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FILESYSTEM_LAYOUT_H_
#define CRYPTOHOME_FILESYSTEM_LAYOUT_H_

#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/platform.h"

namespace cryptohome {

// Name of the vault directory which is used with eCryptfs cryptohome.
inline constexpr char kEcryptfsVaultDir[] = "vault";
// Name of the mount directory.
inline constexpr char kMountDir[] = "mount";
// Name of the temporary mount directory used during migration.
inline constexpr char kTemporaryMountDir[] = "temporary_mount";
// Name of the dm-crypt cache directory.
inline constexpr char kDmcryptCacheDir[] = "cache";
// Device Mapper directory.
inline constexpr char kDeviceMapperDir[] = "/dev/mapper";

// Suffix for cryptohome dm-crypt container.
inline constexpr char kDmcryptCacheContainerSuffix[] = "cache";
inline constexpr char kDmcryptDataContainerSuffix[] = "data";

inline constexpr mode_t kKeyFilePermissions = 0600;
inline constexpr int kKeyFileMax = 100;  // master.0 ... master.99 // nocheck
inline constexpr char kKeyFile[] = "master";  // nocheck
inline constexpr char kKeyLegacyPrefix[] = "legacy-";

inline constexpr int kInitialKeysetIndex = 0;
inline constexpr char kTsFile[] = "timestamp";

inline constexpr char kDmcryptContainerMountType[] = "ext4";
inline constexpr char kDmcryptContainerMountOptions[] = "discard,commit=600";

inline constexpr char kUserSecretStashDir[] = "user_secret_stash";
inline constexpr char kUserSecretStashFile[] = "uss";
inline constexpr char kAuthFactorsDir[] = "auth_factors";

base::FilePath ShadowRoot();
base::FilePath SystemSaltFile();
base::FilePath PublicMountSaltFile();
base::FilePath SkelDir();
base::FilePath UserPath(const std::string& obfuscated);
base::FilePath VaultKeysetPath(const std::string& obfuscated, int index);
base::FilePath UserActivityPerIndexTimestampPath(const std::string& obfuscated,
                                                 int index);
base::FilePath UserActivityTimestampPath(const std::string& obfuscated);
base::FilePath UserSecretStashPath(const std::string& obfuscated_username);
base::FilePath AuthFactorsDirPath(const std::string& obfuscated_username);
base::FilePath AuthFactorPath(const std::string& obfuscated_username,
                              const std::string& auth_factor_type_string,
                              const std::string& auth_factor_label);

std::string LogicalVolumePrefix(const std::string& obfuscated_username);
std::string DmcryptVolumePrefix(const std::string& obfuscated_username);

base::FilePath GetEcryptfsUserVaultPath(const std::string& obfuscated_username);
base::FilePath GetUserMountDirectory(const std::string& obfuscated_username);
base::FilePath GetUserTemporaryMountDirectory(
    const std::string& obfuscated_username);
base::FilePath GetDmcryptUserCacheDirectory(
    const std::string& obfuscated_username);
base::FilePath GetDmcryptDataVolume(const std::string& obfuscated_username);
base::FilePath GetDmcryptCacheVolume(const std::string& obfuscated_username);

bool InitializeFilesystemLayout(Platform* platform,
                                Crypto* crypto,
                                brillo::SecureBlob* salt);

}  // namespace cryptohome

#endif  // CRYPTOHOME_FILESYSTEM_LAYOUT_H_
