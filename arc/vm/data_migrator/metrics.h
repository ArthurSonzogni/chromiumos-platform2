// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_DATA_MIGRATOR_METRICS_H_
#define ARC_VM_DATA_MIGRATOR_METRICS_H_

#include <memory>

#include <base/time/time.h>
#include <cryptohome/data_migrator/metrics.h>
#include <metrics/metrics_library.h>

namespace arc::data_migrator {

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

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;
};

}  // namespace arc::data_migrator

#endif  // ARC_VM_DATA_MIGRATOR_METRICS_H_
