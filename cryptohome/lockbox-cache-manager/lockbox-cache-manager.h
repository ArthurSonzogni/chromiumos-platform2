// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LOCKBOX_CACHE_MANAGER_LOCKBOX_CACHE_MANAGER_H_
#define CRYPTOHOME_LOCKBOX_CACHE_MANAGER_LOCKBOX_CACHE_MANAGER_H_

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "cryptohome/lockbox-cache-manager/metrics.h"
#include "cryptohome/lockbox-cache-manager/platform.h"

namespace cryptohome {

namespace filepaths {
inline constexpr const char* kLocOldInstallAttrs =
    "home/.shadow/install_attributes.pb";
inline constexpr const char* kLocNewInstallAttrs =
    "var/lib/device_management/install_attributes.pb";
inline constexpr const char* kLocLockboxCache =
    "run/lockbox/install_attributes.pb";
inline constexpr const char* kLocLockboxNvram = "tmp/lockbox.nvram";
}  // namespace filepaths

class LockboxCacheManager {
 public:
  explicit LockboxCacheManager(const base::FilePath& root);
  ~LockboxCacheManager() = default;

  void SetParamsForTesting(std::unique_ptr<Platform> platform) {
    platform_ = std::move(platform);
  }

  // Run() has two responsibilities:
  // 1. (optional) Migrate the install-time attributes content.
  // 2. Invoke lockbox-cache tool to create validated install-time attributes
  // cache copy.
  bool Run();
  MigrationStatus MigrateInstallAttributesIfNeeded();
  bool CreateLockboxCache();
  void PopulateLockboxNvramFile();

  // Getters
  base::FilePath GetInstallAttrsOldPath() { return install_attrs_old_path_; }
  base::FilePath GetInstallAttrsNewPath() { return install_attrs_new_path_; }
  base::FilePath GetLockboxCachePath() { return lockbox_cache_path_; }
  base::FilePath GetLockboxNvramFilePath() { return lockbox_nvram_file_path_; }

 private:
  void InvokeLockboxCacheTool();
  std::unique_ptr<Metrics> metrics_;
  std::unique_ptr<Platform> platform_;
  const base::FilePath root_;  // root dir for fs.
  base::FilePath install_attrs_old_path_;
  base::FilePath install_attrs_new_path_;
  base::FilePath lockbox_cache_path_;
  base::FilePath lockbox_nvram_file_path_;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_LOCKBOX_CACHE_MANAGER_LOCKBOX_CACHE_MANAGER_H_
