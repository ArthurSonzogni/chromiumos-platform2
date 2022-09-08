// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/bind.h>
#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <base/time/time.h>

#include "vm_tools/cicerone/guest_metrics.h"

namespace vm_tools {
namespace cicerone {

// chromeos_metrics::CumulativeMetrics constants:
constexpr base::TimeDelta kDailyUpdatePeriod = base::Minutes(5);
constexpr base::TimeDelta kDailyAccumulatePeriod = base::Hours(24);
constexpr char kDailyMetricsBackingDir[] = "/var/lib/vm_cicerone/metrics/daily";

// Borealis metric IDs
constexpr char kBorealisSwapBytesRead[] = "Borealis.Disk.SwapReadsDaily";
constexpr char kBorealisSwapBytesReadGuest[] = "borealis-swap-kb-read";

constexpr char kBorealisSwapBytesWritten[] = "Borealis.Disk.SwapWritesDaily";
constexpr char kBorealisSwapBytesWrittenGuest[] = "borealis-swap-kb-written";

constexpr char kBorealisDiskBytesRead[] = "Borealis.Disk.StatefulReadsDaily";
constexpr char kBorealisDiskBytesReadGuest[] = "borealis-disk-kb-read";

constexpr char kBorealisDiskBytesWritten[] =
    "Borealis.Disk.StatefulWritesDaily";
constexpr char kBorealisDiskBytesWrittenGuest[] = "borealis-disk-kb-written";

GuestMetrics::GuestMetrics()
    : GuestMetrics(base::FilePath(kDailyMetricsBackingDir)) {}

GuestMetrics::GuestMetrics(base::FilePath cumulative_metrics_path)
    : daily_metrics_(cumulative_metrics_path,
                     {kBorealisSwapBytesRead, kBorealisSwapBytesWritten,
                      kBorealisDiskBytesRead, kBorealisDiskBytesWritten},
                     kDailyUpdatePeriod,
                     base::BindRepeating(&GuestMetrics::UpdateDailyMetrics,
                                         base::Unretained(this)),
                     kDailyAccumulatePeriod,
                     base::BindRepeating(&GuestMetrics::ReportDailyMetrics,
                                         base::Unretained(this))),
      metrics_lib_(std::make_unique<MetricsLibrary>()) {}

bool GuestMetrics::HandleMetric(const std::string& vm_name,
                                const std::string& container_name,
                                const std::string& name,
                                int value) {
  // This is the central handling point for all metrics emitted by VMs.  Right
  // now everything ends up stored/reported by daily_metrics_, but this could
  // also handle metrics to be reported immediately (with appropriate rate
  // limiting) or on a different schedule (by adding another CumulativeMetrics
  // instance.)
  if (vm_name == "borealis" && container_name == "penguin") {
    // Metrics emitted by Borealis VMs.
    if (name == kBorealisSwapBytesReadGuest) {
      daily_metrics_.Add(kBorealisSwapBytesRead, value);
    } else if (name == kBorealisSwapBytesWrittenGuest) {
      daily_metrics_.Add(kBorealisSwapBytesWritten, value);
    } else if (name == kBorealisDiskBytesReadGuest) {
      daily_metrics_.Add(kBorealisDiskBytesRead, value);
    } else if (name == kBorealisDiskBytesWrittenGuest) {
      daily_metrics_.Add(kBorealisDiskBytesWritten, value);
    } else {
      LOG(ERROR) << "Unknown Borealis metric " << name;
      return false;
    }
  } else {
    LOG(ERROR) << "No metrics are known for VM " << vm_name << " and container "
               << container_name;
    return false;
  }
  return true;
}

void GuestMetrics::UpdateDailyMetrics(chromeos_metrics::CumulativeMetrics* cm) {
  // This is a no-op; currently all metric data is accumulated in HandleMetric.
}

void GuestMetrics::ReportDailyMetrics(chromeos_metrics::CumulativeMetrics* cm) {
  // Borealis metrics
  int swapin = daily_metrics_.Get(kBorealisSwapBytesRead);
  int swapout = daily_metrics_.Get(kBorealisSwapBytesWritten);
  int blocksin = daily_metrics_.Get(kBorealisDiskBytesRead);
  int blocksout = daily_metrics_.Get(kBorealisDiskBytesWritten);

  // Range chosen to match Platform.StatefulWritesDaily.
  metrics_lib_->SendToUMA(kBorealisSwapBytesRead, swapin, 0, 209715200, 50);
  metrics_lib_->SendToUMA(kBorealisSwapBytesWritten, swapout, 0, 209715200, 50);
  metrics_lib_->SendToUMA(kBorealisDiskBytesRead, blocksin, 0, 209715200, 50);
  metrics_lib_->SendToUMA(kBorealisDiskBytesWritten, blocksout, 0, 209715200,
                          50);
}

}  // namespace cicerone
}  // namespace vm_tools
