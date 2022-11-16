// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_DIRCRYPTO_MIGRATION_HELPER_DELEGATE_H_
#define CRYPTOHOME_STORAGE_DIRCRYPTO_MIGRATION_HELPER_DELEGATE_H_

#include "cryptohome/data_migrator/migration_helper_delegate.h"

namespace cryptohome {

// Delegate class for MigrationHelper that implements logic specific to the Ext4
// migration.
class DircryptoMigrationHelperDelegate
    : public data_migrator::MigrationHelperDelegate {
 public:
  explicit DircryptoMigrationHelperDelegate(MigrationType migration_type);
  ~DircryptoMigrationHelperDelegate() override = default;

  DircryptoMigrationHelperDelegate(const DircryptoMigrationHelperDelegate&) =
      delete;
  DircryptoMigrationHelperDelegate& operator=(
      const DircryptoMigrationHelperDelegate&) = delete;

  // dircrypto_data_migrator::MigrationHelperDelegate overrides:
  bool ShouldReportProgress() override;
  void ReportStartTime() override;
  void ReportEndTime() override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_DIRCRYPTO_MIGRATION_HELPER_DELEGATE_H_
