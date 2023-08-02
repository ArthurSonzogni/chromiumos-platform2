// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MAINTENANCE_SCHEDULER_H_
#define BIOD_MAINTENANCE_SCHEDULER_H_

#include <memory>

#include <base/functional/callback.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "biod/cros_fp_device.h"

namespace biod {

class MaintenanceScheduler {
 public:
  MaintenanceScheduler(ec::CrosFpDeviceInterface* cros_fp_device,
                       BiodMetricsInterface* biod_metrics);
  MaintenanceScheduler(const MaintenanceScheduler&) = delete;
  MaintenanceScheduler& operator=(const MaintenanceScheduler&) = delete;
  ~MaintenanceScheduler() = default;

  // Schedule the maintenance timer to fire after 1 day.
  void Start();

 private:
  void Schedule(base::TimeDelta delta);
  void OnMaintenanceTimerFired();

  std::unique_ptr<base::OneShotTimer> timer_;
  ec::CrosFpDeviceInterface* cros_dev_;
  BiodMetricsInterface* metrics_;
};
}  // namespace biod

#endif  // BIOD_MAINTENANCE_SCHEDULER_H_
