// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CICERONE_GUEST_METRICS_H_
#define VM_TOOLS_CICERONE_GUEST_METRICS_H_

#include <array>
#include <memory>
#include <string>
#include <utility>

#include <metrics/cumulative_metrics.h>
#include <metrics/metrics_library.h>
#include <base/time/time.h>

namespace vm_tools {
namespace cicerone {

// Handler for metrics emitted by VM guests.
class GuestMetrics {
 public:
  GuestMetrics();
  // Specify path for testing
  explicit GuestMetrics(base::FilePath cumulative_metrics_path);
  GuestMetrics(const GuestMetrics&) = delete;
  GuestMetrics& operator=(const GuestMetrics&) = delete;
  virtual ~GuestMetrics() {}

  // Called by Service class upon receiving a ReportMetrics RPC from the guest.
  virtual bool HandleMetric(const std::string& vm_name,
                            const std::string& container_name,
                            const std::string& name,
                            int value);

  // Called by |daily_metrics_| regularly to gather metrics to be reported
  // daily.
  void UpdateDailyMetrics(chromeos_metrics::CumulativeMetrics* cm);

  // Called once a day to send daily metrics to UMA.
  void ReportDailyMetrics(chromeos_metrics::CumulativeMetrics* cm);

  void SetMetricsLibraryForTesting(
      std::unique_ptr<MetricsLibraryInterface> metrics_lib) {
    metrics_lib_ = std::move(metrics_lib);
  }

  MetricsLibraryInterface* metrics_library_for_testing() {
    return metrics_lib_.get();
  }

  void ReportMetricsImmediatelyForTesting() {
    ReportDailyMetrics(&daily_metrics_);
  }

 private:
  // Accumulator for metrics that are to be reported daily.
  chromeos_metrics::CumulativeMetrics daily_metrics_;

  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
};

}  // namespace cicerone
}  // namespace vm_tools

#endif  // VM_TOOLS_CICERONE_GUEST_METRICS_H_
