// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_DATA_MIGRATOR_METRICS_H_
#define ARC_VM_DATA_MIGRATOR_METRICS_H_

#include <memory>

#include <base/files/file.h>
#include <base/time/time.h>
#include <cryptohome/data_migrator/metrics.h>
#include <metrics/metrics_library.h>

namespace arc::data_migrator {

// The result of the setup before triggering MigrationHelper.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class SetupResult {
  // Migration is successfully set up.
  kSuccess = 0,
  // Failed to mkdir the mount point.
  kMountPointCreationFailure = 1,
  // Failed to attach a loop device to the migration destination.
  kLoopDeviceAttachmentFailure = 2,
  // Failed to call mount().
  kMountFailure = 3,
  // Failed to start a new thread for MigrationHelper.
  kThreadStartFailure = 4,
  kMaxValue = kThreadStartFailure,
};

// A class that sends UMA metrics using MetricsLibrary. There is no D-Bus call
// because MetricsLibrary writes the UMA data to /var/lib/metrics/uma-events.
class ArcVmDataMigratorMetrics {
 public:
  ArcVmDataMigratorMetrics();
  ~ArcVmDataMigratorMetrics() = default;
  ArcVmDataMigratorMetrics(const ArcVmDataMigratorMetrics&) = delete;
  ArcVmDataMigratorMetrics& operator=(const ArcVmDataMigratorMetrics&) = delete;

  // Reports the duration of the migration.
  void ReportDuration(base::TimeDelta duration);

  // Reports the start and end status of the migration.
  void ReportStartStatus(
      cryptohome::data_migrator::MigrationStartStatus status);
  void ReportEndStatus(cryptohome::data_migrator::MigrationEndStatus status);

  // Reports the total bytes (in MB) and the number of files to be migrated.
  void ReportTotalByteCountInMb(int total_byte_count_mb);
  void ReportTotalFileCount(int total_file_count);

  // Reports the result of the setup before triggering MigrationHelper.
  void ReportSetupResult(SetupResult result);

  // Reports the error code of a failure.
  void ReportFailedErrorCode(base::File::Error error_code);

  // Reports the type of file operation that caused a failure.
  void ReportFailedOperationType(
      cryptohome::data_migrator::MigrationFailedOperationType type);

  // Reports device's free space at the beginning of the migration in MB.
  void ReportInitialFreeSpace(int initial_free_space_mb);

  // Reports device's free space at the timing of ENOSPC failure in MB.
  void ReportNoSpaceFailureFreeSpace(int failure_free_space_mb);

  // Reports the total bytes of xattr assigned to a file.
  void ReportNoSpaceXattrSize(int total_xattr_size_bytes);

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;
};

}  // namespace arc::data_migrator

#endif  // ARC_VM_DATA_MIGRATOR_METRICS_H_
