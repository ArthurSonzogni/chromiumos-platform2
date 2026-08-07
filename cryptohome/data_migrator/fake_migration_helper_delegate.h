// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DATA_MIGRATOR_FAKE_MIGRATION_HELPER_DELEGATE_H_
#define CRYPTOHOME_DATA_MIGRATOR_FAKE_MIGRATION_HELPER_DELEGATE_H_

#include <sys/stat.h>

#include <optional>
#include <string>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/data_migrator/migration_helper_delegate.h"

namespace cryptohome::data_migrator {

class FakeMigrationHelperDelegate : public MigrationHelperDelegate {
 public:
  FakeMigrationHelperDelegate(libstorage::Platform* platform,
                              const base::FilePath& to_dir);
  ~FakeMigrationHelperDelegate() override;

  FakeMigrationHelperDelegate(const FakeMigrationHelperDelegate&) = delete;
  FakeMigrationHelperDelegate& operator=(const FakeMigrationHelperDelegate&) =
      delete;

  // Adds a path to the migration denylist. The |path| should be a relative path
  // of a file or a directory to the migration source. Adding the path to the
  // denylist makes the file or the directory (including its contents) not
  // migrated to the migration destination.
  void AddDenylistedPath(const base::FilePath& path);

  // Sets the value to be returned by FreeSpaceForMigrator(). When the return
  // value of FreeSpaceForMigrator() has not been set, it falls back to the
  // result of |platform_.AmountOfFreeDiskSpace(to_dir_)|.
  void SetFreeDiskSpaceForMigrator(
      std::optional<int64_t> free_disk_space_for_migrator);

  // MigrationHelperDelegate overrides:
  bool ShouldMigrateFile(const base::FilePath& child) override;
  bool ShouldSkipFileOnIOErrors() override;
  std::optional<int64_t> FreeSpaceForMigrator() override;

 private:
  absl::flat_hash_set<base::FilePath> denylisted_paths_;
  std::optional<int64_t> free_disk_space_for_migrator_;
  const libstorage::Platform* platform_;
  const base::FilePath to_dir_;
};

}  // namespace cryptohome::data_migrator

#endif  // CRYPTOHOME_DATA_MIGRATOR_FAKE_MIGRATION_HELPER_DELEGATE_H_
