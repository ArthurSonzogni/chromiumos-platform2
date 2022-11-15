// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/dircrypto_migration_helper_delegate.h"

#include "cryptohome/cryptohome_metrics.h"

namespace cryptohome {

DircryptoMigrationHelperDelegate::DircryptoMigrationHelperDelegate(
    MigrationType migration_type)
    : MigrationHelperDelegate(migration_type) {}

bool DircryptoMigrationHelperDelegate::ShouldReportProgress() {
  // Don't report progress in minimal migration as we're skipping most of data.
  return migration_type() == MigrationType::FULL;
}

void DircryptoMigrationHelperDelegate::ReportStartTime() {
  const auto migration_timer_id = migration_type() == MigrationType::MINIMAL
                                      ? kDircryptoMinimalMigrationTimer
                                      : kDircryptoMigrationTimer;
  ReportTimerStart(migration_timer_id);
}

void DircryptoMigrationHelperDelegate::ReportEndTime() {
  const auto migration_timer_id = migration_type() == MigrationType::MINIMAL
                                      ? kDircryptoMinimalMigrationTimer
                                      : kDircryptoMigrationTimer;
  ReportTimerStop(migration_timer_id);
}

}  // namespace cryptohome
