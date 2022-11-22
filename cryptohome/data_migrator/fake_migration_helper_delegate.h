// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DATA_MIGRATOR_FAKE_MIGRATION_HELPER_DELEGATE_H_
#define CRYPTOHOME_DATA_MIGRATOR_FAKE_MIGRATION_HELPER_DELEGATE_H_

#include <unordered_set>

#include <base/files/file_path.h>

#include "cryptohome/data_migrator/migration_helper_delegate.h"

namespace cryptohome::data_migrator {

class FakeMigrationHelperDelegate : public MigrationHelperDelegate {
 public:
  FakeMigrationHelperDelegate();
  ~FakeMigrationHelperDelegate() override;

  FakeMigrationHelperDelegate(const FakeMigrationHelperDelegate&) = delete;
  FakeMigrationHelperDelegate& operator=(const FakeMigrationHelperDelegate&) =
      delete;

  // Adds a path to the migration denylist. The |path| should be a relative path
  // of a file or a directory to the migration source. Adding the path to the
  // denylist makes the file or the directory (including its contents) not
  // migrated to the migration destination.
  void AddDenylistedPath(const base::FilePath& path);

  // Clears all the denylisted paths added so far.
  void ClearDenylistedPaths();

  // dircrypto_data_migrator::MigrationHelperDelegate overrides:
  bool ShouldMigrateFile(const base::FilePath& child) override;
  bool ShouldCopyQuotaProjectId() override;

 private:
  std::unordered_set<base::FilePath> denylisted_paths_;
};

}  // namespace cryptohome::data_migrator

#endif  // CRYPTOHOME_DATA_MIGRATOR_FAKE_MIGRATION_HELPER_DELEGATE_H_
