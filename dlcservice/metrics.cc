// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstdint>

#include <base/check.h>
#include <base/logging.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/error.h"
#include "dlcservice/metrics.h"

using dlcservice::metrics::InstallResult;
using dlcservice::metrics::UninstallResult;
using std::string;

namespace dlcservice {

namespace metrics {

const char kMetricInstallResult[] = "Platform.DlcService.InstallResult";
const char kMetricUninstallResult[] = "Platform.DlcService.UninstallResult";

constexpr char kMetricTotalUsedMBytes[] = "Platform.DlcService.TotalUsedMBytes";
constexpr int kMetricTotalUsedMBytesMax = 1 * 1024 * 1024;  // 1 TiB.
constexpr int kMetricTotalUsedMBytesMin = 1;
constexpr int kMetricTotalUsedMBytesNumBuckets = 50;

extern const char kMetricsPrefsDir[] = "metrics";
extern const char kMetricsLastReportTimePref[] = "last_report_time";
}  // namespace metrics

// IMPORTANT: To obsolete a metric enum value, just remove it from the map
// initialization and comment it out on the Enum.
Metrics::InstallResultMap Metrics::install_result_ = {
    {error::kFailedToCreateDirectory, InstallResult::kFailedToCreateDirectory},
    {error::kFailedInstallInUpdateEngine,
     InstallResult::kFailedInstallInUpdateEngine},
    {kErrorInvalidDlc, InstallResult::kFailedInvalidDlc},      // dbus error
    {kErrorNeedReboot, InstallResult::kFailedNeedReboot},      // dbus error
    {kErrorBusy, InstallResult::kFailedUpdateEngineBusy},      // dbus error
    {kErrorNoImageFound, InstallResult::kFailedNoImageFound},  // dbus error
    {error::kFailedToVerifyImage, InstallResult::kFailedToVerifyImage},
    {error::kFailedToMountImage, InstallResult::kFailedToMountImage},
};

Metrics::UninstallResultMap Metrics::uninstall_result_ = {
    {kErrorInvalidDlc, UninstallResult::kFailedInvalidDlc},  // dbus error
    {kErrorBusy, UninstallResult::kFailedUpdateEngineBusy},  // dbus error
};

void Metrics::Init() {}

void Metrics::SendInstallResultSuccess(const bool& installed_by_ue) {
  if (installed_by_ue) {
    SendInstallResult(InstallResult::kSuccessNewInstall);
  } else {
    SendInstallResult(InstallResult::kSuccessAlreadyInstalled);
  }
}

void Metrics::SendInstallResultFailure(brillo::ErrorPtr* err) {
  DCHECK(err->get());
  InstallResult res = InstallResult::kUnknownError;
  if (err && err->get()) {
    const string error_code = Error::GetRootErrorCode(*err);
    auto it = install_result_.find(error_code);
    if (it != install_result_.end()) {
      res = it->second;
    }
  }
  SendInstallResult(res);
}

void Metrics::SendInstallResult(InstallResult result) {
  metrics_library_->SendEnumToUMA(
      metrics::kMetricInstallResult, static_cast<int>(result),
      static_cast<int>(InstallResult::kNumConstants));
  LOG(INFO) << "InstallResult metric sent:" << static_cast<int>(result);
}

void Metrics::SendUninstallResult(brillo::ErrorPtr* err) {
  UninstallResult res = UninstallResult::kUnknownError;
  if (err && err->get()) {
    const string error_code = Error::GetRootErrorCode(*err);
    auto it = uninstall_result_.find(error_code);
    if (it != uninstall_result_.end()) {
      res = it->second;
    }
  } else {
    res = UninstallResult::kSuccess;
  }
  SendUninstallResult(res);
}

void Metrics::SendUninstallResult(UninstallResult result) {
  metrics_library_->SendEnumToUMA(
      metrics::kMetricUninstallResult, static_cast<int>(result),
      static_cast<int>(UninstallResult::kNumConstants));
}

void Metrics::SendTotalUsedOnDisk(uint64_t used_bytes) {
  // Convert to MiB (round up) with an upper limit.
  constexpr int kDivMiB = 1024 * 1024;
  auto used_mb =
      std::min<uint64_t>(used_bytes / kDivMiB + (used_bytes % kDivMiB != 0),
                         metrics::kMetricTotalUsedMBytesMax);

  metrics_library_->SendToUMA(metrics::kMetricTotalUsedMBytes, used_mb,
                              metrics::kMetricTotalUsedMBytesMin,
                              metrics::kMetricTotalUsedMBytesMax,
                              metrics::kMetricTotalUsedMBytesNumBuckets);
}

}  // namespace dlcservice
