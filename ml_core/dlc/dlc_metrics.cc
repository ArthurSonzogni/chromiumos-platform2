// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_core/dlc/dlc_metrics.h"

#include <string>

#include <dlcservice/dbus-constants.h>
#include <metrics/metrics_library.h>

namespace cros {

DlcErrorCode DlcErrorCodeEnumFromString(std::string_view error) {
  if (error == dlcservice::kErrorNone) {
    return DlcErrorCode::kNone;
  } else if (error == dlcservice::kErrorInternal) {
    return DlcErrorCode::kInternal;
  } else if (error == dlcservice::kErrorBusy) {
    return DlcErrorCode::kBusy;
  } else if (error == dlcservice::kErrorNeedReboot) {
    return DlcErrorCode::kNeedReboot;
  } else if (error == dlcservice::kErrorInvalidDlc) {
    return DlcErrorCode::kInvalidDlc;
  } else if (error == dlcservice::kErrorAllocation) {
    return DlcErrorCode::kAllocation;
  } else if (error == dlcservice::kErrorNoImageFound) {
    return DlcErrorCode::kNoImageFound;
  }

  return DlcErrorCode::kUnrecognized;
}

DlcMetrics::DlcMetrics() = default;
DlcMetrics::~DlcMetrics() = default;

void DlcMetrics::SetMetricsBaseName(const std::string& metrics_base_name) {
  metrics_base_name_ = metrics_base_name;
}

void DlcMetrics::RecordBeginInstallResult(const DlcBeginInstallResult result) {
  if (!metrics_base_name_.empty()) {
    metrics_library_.SendEnumToUMA(
        metrics_base_name_ + ".DlcBeginInstallResult", result);
  }
}

void DlcMetrics::RecordBeginInstallDlcServiceError(const DlcErrorCode error) {
  if (!metrics_base_name_.empty()) {
    metrics_library_.SendEnumToUMA(
        metrics_base_name_ + ".DlcBeginInstallDlcServiceError", error);
  }
}

void DlcMetrics::RecordFinalInstallResult(const DlcFinalInstallResult result) {
  if (!metrics_base_name_.empty()) {
    metrics_library_.SendEnumToUMA(
        metrics_base_name_ + ".DlcFinalInstallResult", result);
  }
}

void DlcMetrics::RecordFinalInstallDlcServiceError(const DlcErrorCode error) {
  if (!metrics_base_name_.empty()) {
    metrics_library_.SendEnumToUMA(
        metrics_base_name_ + ".DlcFinalInstallDlcServiceError", error);
  }
}

void DlcMetrics::RecordInstallAttemptCount(int n, int max) {
  if (!metrics_base_name_.empty()) {
    metrics_library_.SendLinearToUMA(
        metrics_base_name_ + ".DlcInstallAttemptCount", n, max);
  }
}

}  // namespace cros
