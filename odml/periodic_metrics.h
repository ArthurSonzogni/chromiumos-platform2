// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_PERIODIC_METRICS_H_
#define ODML_PERIODIC_METRICS_H_

#include <memory>

#include <base/process/process_metrics.h>
#include <metrics/cumulative_metrics.h>
#include <metrics/metrics_library.h>

namespace odml {

// Performs periodic UMA metrics logging for the ODML Service daemon.
// Periodically gathers some process metrics (e.g. memory, CPU usage) itself.
// Threading: Create and use on a single sequence.
class PeriodicMetrics {
 public:
  explicit PeriodicMetrics(raw_ref<MetricsLibrary> metrics);
  PeriodicMetrics(const PeriodicMetrics&) = delete;
  PeriodicMetrics& operator=(const PeriodicMetrics&) = delete;

  // Starts periodic sampling of process metrics.
  void StartCollectingProcessMetrics();

  // Fetches process metrics (e.g. RAM) and updates `cumulative_metrics`.
  // If `record_current_metrics` is true, also logs current process metrics.
  void UpdateAndRecordMetricsNow();

 private:
  // Fetches process metrics (e.g. RAM) and updates `cumulative_metrics`.
  // If `record_current_metrics` is true, also logs current process metrics.
  void UpdateAndRecordMetrics(
      chromeos_metrics::CumulativeMetrics* cumulative_metrics);

  // Uploads process metrics.
  void UploadMetrics(chromeos_metrics::CumulativeMetrics* cumulative_metrics);

  raw_ref<MetricsLibrary> metrics_;
  std::unique_ptr<base::ProcessMetrics> process_metrics_;
  std::unique_ptr<chromeos_metrics::CumulativeMetrics> cumulative_metrics_;
  base::RepeatingTimer timer_;

  // Must be last class member.
  base::WeakPtrFactory<PeriodicMetrics> weak_factory_{this};
};

}  // namespace odml

#endif  // ODML_PERIODIC_METRICS_H_
