// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/dircrypto_migration_helper_delegate.h"

#include <string>
#include <vector>

#include "cryptohome/cryptohome_metrics.h"

namespace cryptohome {

namespace {

struct PathTypeMapping {
  const char* path;
  DircryptoMigrationFailedPathType type;
};

const PathTypeMapping kPathTypeMappings[] = {
    {"root/android-data", kMigrationFailedUnderAndroidOther},
    {"user/Downloads", kMigrationFailedUnderDownloads},
    {"user/Cache", kMigrationFailedUnderCache},
    {"user/GCache", kMigrationFailedUnderGcache},
};

}  // namespace

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

void DircryptoMigrationHelperDelegate::ReportStartStatus(
    data_migrator::MigrationStartStatus status) {
  ReportDircryptoMigrationStartStatus(migration_type(), status);
}

void DircryptoMigrationHelperDelegate::ReportEndStatus(
    data_migrator::MigrationEndStatus status) {
  ReportDircryptoMigrationEndStatus(migration_type(), status);
}

void DircryptoMigrationHelperDelegate::ReportFailure(
    base::File::Error error_code,
    data_migrator::MigrationFailedOperationType operation_type,
    const base::FilePath& path) {
  DircryptoMigrationFailedPathType path_type = kMigrationFailedUnderOther;
  for (const auto& path_type_mapping : kPathTypeMappings) {
    if (base::FilePath(path_type_mapping.path).IsParent(path)) {
      path_type = path_type_mapping.type;
      break;
    }
  }
  // Android cache files are either under
  //   root/android-data/data/data/<package name>/cache
  //   root/android-data/data/media/0/Android/data/<package name>/cache
  if (path_type == kMigrationFailedUnderAndroidOther) {
    std::vector<std::string> components = path.GetComponents();
    if ((components.size() >= 7u && components[2] == "data" &&
         components[3] == "data" && components[5] == "cache") ||
        (components.size() >= 10u && components[2] == "data" &&
         components[3] == "media" && components[4] == "0" &&
         components[5] == "Android" && components[6] == "data" &&
         components[8] == "cache")) {
      path_type = kMigrationFailedUnderAndroidCache;
    }
  }

  ReportDircryptoMigrationFailedOperationType(operation_type);
  ReportDircryptoMigrationFailedPathType(path_type);
  ReportDircryptoMigrationFailedErrorCode(error_code);
}

void DircryptoMigrationHelperDelegate::ReportTotalSize(int total_byte_count_mb,
                                                       int total_file_count) {
  ReportDircryptoMigrationTotalByteCountInMb(total_byte_count_mb);
  ReportDircryptoMigrationTotalFileCount(total_file_count);
}

void DircryptoMigrationHelperDelegate::ReportFailedNoSpace(
    int initial_migration_free_space_mb, int failure_free_space_mb) {
  ReportDircryptoMigrationFailedNoSpace(initial_migration_free_space_mb,
                                        failure_free_space_mb);
}

void DircryptoMigrationHelperDelegate::ReportFailedNoSpaceXattrSizeInBytes(
    int total_xattr_size_bytes) {
  ReportDircryptoMigrationFailedNoSpaceXattrSizeInBytes(total_xattr_size_bytes);
}

}  // namespace cryptohome
