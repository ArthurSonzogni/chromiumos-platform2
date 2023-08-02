// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/maintenance_scheduler.h"

#include <memory>

#include <base/functional/callback.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "biod/cros_fp_device.h"
#include "libec/fingerprint/fp_mode.h"

namespace biod {

namespace {

using Mode = ec::FpMode::Mode;

const base::TimeDelta kMaintenanceInterval = base::Days(1);
const base::TimeDelta kMaintenanceRetryInterval = base::Minutes(10);
}  // namespace

MaintenanceScheduler::MaintenanceScheduler(
    ec::CrosFpDeviceInterface* cros_fp_device,
    BiodMetricsInterface* biod_metrics)
    : timer_(std::make_unique<base::OneShotTimer>()),
      cros_dev_(cros_fp_device),
      metrics_(biod_metrics) {
  CHECK(cros_dev_);
}

void MaintenanceScheduler::Start() {
  Schedule(kMaintenanceInterval);
}

void MaintenanceScheduler::Schedule(base::TimeDelta delta) {
  timer_->Start(FROM_HERE, delta,
                base::BindOnce(&MaintenanceScheduler::OnMaintenanceTimerFired,
                               base::Unretained(this)));
}

void MaintenanceScheduler::OnMaintenanceTimerFired() {
  auto fp_sensor_mode = cros_dev_->GetFpMode();
  if (fp_sensor_mode != ec::FpMode(Mode::kNone)) {
    LOG(INFO) << "Rescheduling maintenance due to fp_sensor_mode: "
              << fp_sensor_mode;
    Schedule(kMaintenanceRetryInterval);
    return;
  }
  LOG(INFO) << "Maintenance timer fired";

  // Report the number of dead pixels
  cros_dev_->UpdateFpInfo();
  metrics_->SendDeadPixelCount(cros_dev_->DeadPixelCount());

  // The maintenance operation can take a couple hundred milliseconds, so it's
  // an asynchronous mode (the state is cleared by the FPMCU after it is
  // finished with the operation).
  cros_dev_->SetFpMode(ec::FpMode(Mode::kSensorMaintenance));
  Schedule(kMaintenanceInterval);
}

}  // namespace biod
