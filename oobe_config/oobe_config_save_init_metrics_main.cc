// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "oobe_config/metrics/enterprise_rollback_metrics_handler.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_tracking.h"

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  LOG(INFO) << "Starting oobe_config_save_init_metrics";

  oobe_config::EnterpriseRollbackMetricsHandler rollback_metrics;
  if (!oobe_config::StartNewMigrationTracking(rollback_metrics)) {
    LOG(WARNING) << "Failed to start tracking device migration metrics.";
  }

  // Always exit 0 so Upstart pre-start script does not fail.
  return 0;
}
