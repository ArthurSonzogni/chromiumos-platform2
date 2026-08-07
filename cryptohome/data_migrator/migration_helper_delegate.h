// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_
#define CRYPTOHOME_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_

#include <optional>
#include <string>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace cryptohome::data_migrator {

// Delegate class for MigrationHelper that handles logic specific to the type of
// the migration.
class BRILLO_EXPORT MigrationHelperDelegate {
 public:
  MigrationHelperDelegate() = default;
  virtual ~MigrationHelperDelegate() = default;

  MigrationHelperDelegate(const MigrationHelperDelegate&) = delete;
  MigrationHelperDelegate& operator=(const MigrationHelperDelegate&) = delete;

  // Returns whether MigrationHelper should occasionally report the progress of
  // the migration, which includes the bytes already migrated and the total
  // bytes to be migrated.
  virtual bool ShouldReportProgress() { return true; }

  // Returns true if |path| (relative path from the root directory of the
  // migration source) should be migrated. false means that it will be deleted
  // from the migration source, but not copied to the migration destination.
  virtual bool ShouldMigrateFile(const base::FilePath& path) { return true; }

  // Returns true if MigrationHelper should skip migrating a file when it
  // encounters EIO on opening the file. If this returns true,
  // RecordSkippedFile() is called with the name of the file that failed to open
  // with EIO. Returning false means that the EIO failure causes the entire
  // migration to fail.
  virtual bool ShouldSkipFileOnIOErrors() { return false; }

  // Records the name of a file that is skipped during the migration due to file
  // IO error on opening it. |path| is a relative path from migration source.
  virtual void RecordSkippedFile(const base::FilePath& path) {}

  // Returns the amount of free space in bytes that MigrationHelper can use.
  virtual std::optional<int64_t> FreeSpaceForMigrator() = 0;
};

}  // namespace cryptohome::data_migrator

#endif  // CRYPTOHOME_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_
