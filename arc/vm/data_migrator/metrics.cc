// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/data_migrator/metrics.h"

#include <base/numerics/safe_conversions.h>

using cryptohome::data_migrator::MigrationEndStatus;
using cryptohome::data_migrator::MigrationStartStatus;

namespace arc::data_migrator {

namespace {

constexpr char kDuration[] = "Arc.VmDataMigration.Duration";
constexpr char kStartStatus[] = "Arc.VmDataMigration.StartStatus";
constexpr char kEndStatus[] = "Arc.VmDataMigration.EndStatus";
constexpr char kTotalSizeMb[] = "Arc.VmDataMigration.TotalSizeMB";
constexpr char kTotalFileCount[] = "Arc.VmDataMigration.TotalFiles";

constexpr int kNumBuckets = 50;

}  // namespace

ArcVmDataMigratorMetrics::ArcVmDataMigratorMetrics()
    : metrics_library_(std::make_unique<MetricsLibrary>()) {}

void ArcVmDataMigratorMetrics::ReportDuration(base::TimeDelta duration) {
  constexpr int kMin = 1, kMax = 3600 /* 1 hour */;
  metrics_library_->SendToUMA(kDuration,
                              base::saturated_cast<int>(duration.InSeconds()),
                              kMin, kMax, kNumBuckets);
}

void ArcVmDataMigratorMetrics::ReportStartStatus(MigrationStartStatus status) {
  metrics_library_->SendEnumToUMA(
      kStartStatus, static_cast<int>(status),
      static_cast<int>(MigrationStartStatus::kMigrationStartStatusNumBuckets));
}

void ArcVmDataMigratorMetrics::ReportEndStatus(MigrationEndStatus status) {
  metrics_library_->SendEnumToUMA(
      kEndStatus, static_cast<int>(status),
      static_cast<int>(MigrationEndStatus::kMigrationEndStatusNumBuckets));
}

void ArcVmDataMigratorMetrics::ReportTotalByteCountInMb(
    int total_byte_count_mb) {
  constexpr int kMin = 1, kMax = 1 << 20 /* 1 TB */;
  metrics_library_->SendToUMA(kTotalSizeMb, total_byte_count_mb, kMin, kMax,
                              kNumBuckets);
}

void ArcVmDataMigratorMetrics::ReportTotalFileCount(int total_file_count) {
  constexpr int kMin = 1, kMax = 1 << 20 /* 1M files */;
  metrics_library_->SendToUMA(kTotalFileCount, total_file_count, kMin, kMax,
                              kNumBuckets);
}

}  // namespace arc::data_migrator
