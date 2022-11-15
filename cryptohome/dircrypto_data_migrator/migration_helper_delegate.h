// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_
#define CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_

#include "cryptohome/migration_type.h"

namespace cryptohome::dircrypto_data_migrator {

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

  // TODO(b/258402655): Move more Ext4-migration-specific part of
  // dircrypto_data_migrator to this class.

 private:
  const MigrationType migration_type_;
};

}  // namespace cryptohome::dircrypto_data_migrator

#endif  // CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_MIGRATION_HELPER_DELEGATE_H_
