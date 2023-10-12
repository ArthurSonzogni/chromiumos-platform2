// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_TRACKING_H_
#define OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_TRACKING_H_

#include <memory>
#include <string>

#include <base/types/expected.h>
#include <base/version.h>
#include <brillo/brillo_export.h>

#include <oobe_config/metrics/enterprise_rollback_metrics_handler.h>

namespace oobe_config {

std::optional<base::Version> GetDeviceVersion();

// Stops any enterprise rollback tracking. Returns false if there is an error
// cleaning up the tracking file.
BRILLO_EXPORT bool CleanOutdatedTracking(
    oobe_config::EnterpriseRollbackMetricsHandler& rolback_metrics);

// Returns true if there is an ongoing rollback tracking and it corresponds
// to `target_version_policy`.
BRILLO_EXPORT base::expected<bool, std::string>
IsTrackingForRollbackTargetVersion(
    oobe_config::EnterpriseRollbackMetricsHandler& rolback_metrics,
    std::string target_version_policy);

// Starts a new tracking. Returns true if rollback tracking starts successfully.
BRILLO_EXPORT bool StartNewTracking(
    oobe_config::EnterpriseRollbackMetricsHandler& rolback_metrics,
    std::string target_version_policy);

}  // namespace oobe_config

#endif  // OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_TRACKING_H_
