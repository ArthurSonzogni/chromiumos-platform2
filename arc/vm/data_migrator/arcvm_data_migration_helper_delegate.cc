// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/data_migrator/arcvm_data_migration_helper_delegate.h"

namespace arc {

ArcVmDataMigrationHelperDelegate::ArcVmDataMigrationHelperDelegate() = default;

ArcVmDataMigrationHelperDelegate::~ArcVmDataMigrationHelperDelegate() = default;

bool ArcVmDataMigrationHelperDelegate::ShouldCopyQuotaProjectId() {
  return true;
}

}  // namespace arc
