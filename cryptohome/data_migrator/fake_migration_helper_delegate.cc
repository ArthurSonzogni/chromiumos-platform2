// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/data_migrator/fake_migration_helper_delegate.h"

#include <string>

#include <base/files/file_path.h>
#include <libstorage/platform/platform.h>

namespace cryptohome::data_migrator {

FakeMigrationHelperDelegate::FakeMigrationHelperDelegate(
    libstorage::Platform* platform, const base::FilePath& to_dir)
    : platform_(platform), to_dir_(to_dir) {}

FakeMigrationHelperDelegate::~FakeMigrationHelperDelegate() = default;

void FakeMigrationHelperDelegate::AddDenylistedPath(
    const base::FilePath& path) {
  denylisted_paths_.insert(path);
}

void FakeMigrationHelperDelegate::SetFreeDiskSpaceForMigrator(
    std::optional<int64_t> free_disk_space_for_migrator) {
  free_disk_space_for_migrator_ = free_disk_space_for_migrator;
}

bool FakeMigrationHelperDelegate::ShouldMigrateFile(
    const base::FilePath& child) {
  return !denylisted_paths_.contains(child);
}

bool FakeMigrationHelperDelegate::ShouldSkipFileOnIOErrors() {
  return true;
}

std::optional<int64_t> FakeMigrationHelperDelegate::FreeSpaceForMigrator() {
  if (free_disk_space_for_migrator_.has_value()) {
    return free_disk_space_for_migrator_.value();
  }
  return platform_->AmountOfFreeDiskSpace(to_dir_);
}

}  // namespace cryptohome::data_migrator
