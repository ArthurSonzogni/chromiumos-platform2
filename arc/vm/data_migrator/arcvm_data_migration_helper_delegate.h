// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_DATA_MIGRATOR_ARCVM_DATA_MIGRATION_HELPER_DELEGATE_H_
#define ARC_VM_DATA_MIGRATOR_ARCVM_DATA_MIGRATION_HELPER_DELEGATE_H_

#include <string>

#include <cryptohome/data_migrator/migration_helper_delegate.h>

namespace arc {

// Delegate class for cryptohome::data_migrator::MigrationHelper that implements
// logic specific to ARCVM /data migration.
class ArcVmDataMigrationHelperDelegate
    : public cryptohome::data_migrator::MigrationHelperDelegate {
 public:
  ArcVmDataMigrationHelperDelegate();
  ~ArcVmDataMigrationHelperDelegate() override;

  ArcVmDataMigrationHelperDelegate(const ArcVmDataMigrationHelperDelegate&) =
      delete;
  ArcVmDataMigrationHelperDelegate& operator=(
      const ArcVmDataMigrationHelperDelegate&) = delete;

  // cryptohome::data_migrator::MigrationHelperDelegate overrides:
  bool ShouldCopyQuotaProjectId() override;
  std::string GetMtimeXattrName() override;
  std::string GetAtimeXattrName() override;
};

}  // namespace arc

#endif  // ARC_VM_DATA_MIGRATOR_ARCVM_DATA_MIGRATION_HELPER_DELEGATE_H_
