// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THINPOOL_MIGRATOR_MIGRATION_METRICS_H_
#define THINPOOL_MIGRATOR_MIGRATION_METRICS_H_

#include <string>

#include <brillo/brillo_export.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>

namespace thinpool_migrator {

// Tracks the success/failure rate of the migration.
inline constexpr char kResultHistogram[] = "Platform.ThinpoolMigration.Result";
// Tracks whether the migration completed after interruption/transient failures.
inline constexpr char kTriesHistogram[] =
    "Platform.ThinpoolMigration.TriesLeftAtCompletion";
// Measures the total time taken by the migration to help diagnose slow
// migrations.
inline constexpr char kTotalTimeHistogram[] =
    "Platform.ThinpoolMigration.TotalTime";
// The following metrics measure the breakdown of total time to help identify
// potential slow paths in the migration.
inline constexpr char kResizeTimeHistogram[] =
    "Platform.ThinpoolMigration.ResizeTime";
inline constexpr char kThinpoolMetadataTimeHistogram[] =
    "Platform.ThinpoolMigration.ThinpoolMetadataTime";
inline constexpr char kLvmMetadataTimeHistogram[] =
    "Platform.ThinpoolMigration.LvmMetadataTime";
inline constexpr char kRevertTimeHistogram[] =
    "Platform.ThinpoolMigration.RevertTime";

inline constexpr int kMaxTries = 5;

// Initializes metrics. If this is not called, all calls to Report* will have no
// effect.
void BRILLO_EXPORT InitializeMetrics();

// Cleans up metrics.
void TearDownMetrics();

// Override metrics library for testing.
void OverrideMetricsLibraryForTestign(MetricsLibraryInterface* lib);

// Reset internally used MetricsLibrary for testing.
void ClearMetricsLibraryForTesting();

// Reports an integer metric. Used for result and tries.
void BRILLO_EXPORT ReportIntMetric(const std::string& metric, int val, int max);

class ScopedTimerReporter : public chromeos_metrics::TimerReporter {
 public:
  explicit ScopedTimerReporter(const std::string& histogram_name);
  ~ScopedTimerReporter();
};

}  // namespace thinpool_migrator

#endif  // THINPOOL_MIGRATOR_MIGRATION_METRICS_H_
