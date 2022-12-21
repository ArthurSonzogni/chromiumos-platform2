// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_DIRCRYPTO_MIGRATION_HELPER_DELEGATE_H_
#define CRYPTOHOME_STORAGE_DIRCRYPTO_MIGRATION_HELPER_DELEGATE_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>

#include "cryptohome/data_migrator/migration_helper.h"
#include "cryptohome/data_migrator/migration_helper_delegate.h"
#include "cryptohome/migration_type.h"

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
  bool ShouldMigrateFile(const base::FilePath& child) override;
  bool ShouldCopyQuotaProjectId() override;
  std::string GetMtimeXattrName() override;
  std::string GetAtimeXattrName() override;
  void ReportStartTime() override;
  void ReportEndTime() override;
  void ReportStartStatus(data_migrator::MigrationStartStatus status) override;
  void ReportEndStatus(data_migrator::MigrationEndStatus status) override;
  void ReportTotalSize(int total_byte_count_mb, int total_file_count) override;
  void ReportFailure(base::File::Error error_code,
                     data_migrator::MigrationFailedOperationType type,
                     const base::FilePath& path) override;
  void ReportFailedNoSpace(int initial_migration_free_space_mb,
                           int failure_free_space_mb) override;
  void ReportFailedNoSpaceXattrSizeInBytes(int total_xattr_size_bytes) override;

 private:
  const MigrationType migration_type_;

  // Allowlisted paths for minimal migration. May contain directories and files.
  std::vector<base::FilePath> minimal_migration_paths_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_DIRCRYPTO_MIGRATION_HELPER_DELEGATE_H_
