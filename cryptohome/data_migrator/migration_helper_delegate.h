// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_
#define CRYPTOHOME_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_

#include <base/files/file.h>
#include <base/files/file_path.h>

#include "cryptohome/data_migrator/metrics.h"
#include "cryptohome/migration_type.h"

namespace cryptohome::data_migrator {

// Delegate class for MigrationHelper that handles logic specific to the type of
// the migration.
class MigrationHelperDelegate {
 public:
  // TODO(b/258402655): Remove this constructor, the data member
  // |migration_type_| and its getter migration_type() after removing dependency
  // on MigrationType from MigrationHelper.
  explicit MigrationHelperDelegate(MigrationType migration_type)
      : migration_type_(migration_type) {}
  virtual ~MigrationHelperDelegate() = default;

  MigrationHelperDelegate(const MigrationHelperDelegate&) = delete;
  MigrationHelperDelegate& operator=(const MigrationHelperDelegate&) = delete;

  virtual MigrationType migration_type() { return migration_type_; }

  // Returns whether MigrationHelper should occasionally report the progress of
  // the migration, which includes the bytes already migrated and the total
  // bytes to be migrated.
  virtual bool ShouldReportProgress() { return true; }

  // Reports the current time as the migration start time.
  virtual void ReportStartTime() {}
  // Reports the current time as the migration end time.
  virtual void ReportEndTime() {}

  // Reports the migration start status.
  virtual void ReportStartStatus(MigrationStartStatus status) {}
  // Reports the migration end status.
  virtual void ReportEndStatus(MigrationEndStatus status) {}

  // Reports the total bytes in MiB and the total number of files (regular
  // files, directories and symlinks) to be migrated.
  // Called before the migration starts.
  virtual void ReportTotalSize(int total_byte_count_mb, int total_file_count) {}

  // Called when a migration failure happens. Reports the error code, the failed
  // operation type, and the relative path from the root of migration to the
  // failed file.
  virtual void ReportFailure(base::File::Error error_code,
                             MigrationFailedOperationType type,
                             const base::FilePath& path) {}

  // Called when ENOSPC failure happens. Reports the amount of free disk space
  // measured before the migration (|initial_migration_free_space_mb|) and at
  // the time of the failure (|failure_free_space_mb|) in MiB.
  virtual void ReportFailedNoSpace(int initial_migration_free_space_mb,
                                   int failure_free_space_mb) {}

  // Called when ENOSPC failure happens when trying to set xattr on a file.
  // Reports in bytes the sum of the total size of xattr already set on a file
  // and the size of an xattr attempted to be set on the file.
  virtual void ReportFailedNoSpaceXattrSizeInBytes(int total_xattr_size_bytes) {
  }

  // TODO(b/258402655): Move more Ext4-migration-specific part of
  // data_migrator to this class.

 private:
  const MigrationType migration_type_;
};

}  // namespace cryptohome::data_migrator

#endif  // CRYPTOHOME_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_
