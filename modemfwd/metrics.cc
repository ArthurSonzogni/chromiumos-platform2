// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <base/logging.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dbus/modemfwd/dbus-constants.h>

#include "modemfwd/error.h"
#include "modemfwd/logging.h"
#include "modemfwd/metrics.h"

using modemfwd::metrics::DlcInstallResult;
using modemfwd::metrics::DlcUninstallResult;
using modemfwd::metrics::FwUpdateLocation;
using std::string;

namespace modemfwd {

namespace metrics {

const char kMetricDlcInstallResult[] = "Platform.Modemfwd.DlcInstallResult";
const char kMetricDlcUninstallResult[] = "Platform.Modemfwd.DlcUninstallResult";
const char kMetricFwUpdateLocation[] = "Platform.Modemfwd.FWUpdateLocation";

}  // namespace metrics

// IMPORTANT: To obsolete a metric enum value, just remove it from the map
// initialization and comment it out on the Enum.
Metrics::DlcInstallResultMap Metrics::install_result_ = {
    {dlcservice::kErrorInvalidDlc,
     DlcInstallResult::kDlcServiceReturnedInvalidDlc},  // dbus error
    {dlcservice::kErrorAllocation,
     DlcInstallResult::kDlcServiceReturnedAllocation},  // dbus error
    {dlcservice::kErrorNoImageFound,
     DlcInstallResult::kDlcServiceReturnedNoImageFound},  // dbus error
    {dlcservice::kErrorNeedReboot,
     DlcInstallResult::kDlcServiceReturnedNeedReboot},  // dbus error
    {dlcservice::kErrorBusy,
     DlcInstallResult::kDlcServiceReturnedBusy},  // dbus error
    {error::kUnexpectedDlcState, DlcInstallResult::kFailedUnexpectedDlcState},
    {error::kTimeoutWaitingForDlcService,
     DlcInstallResult::kFailedTimeoutWaitingForDlcService},
    {error::kTimeoutWaitingForDlcInstall,
     DlcInstallResult::kFailedTimeoutWaitingForDlcInstall},
    {error::kTimeoutWaitingForInstalledState,
     DlcInstallResult::kFailedTimeoutWaitingForInstalledState},
    {error::kDlcServiceReturnedErrorOnInstall,
     DlcInstallResult::kDlcServiceReturnedErrorOnInstall},
    {error::kDlcServiceReturnedErrorOnGetDlcState,
     DlcInstallResult::kDlcServiceReturnedErrorOnGetDlcState},
    {error::kUnexpectedEmptyDlcId, DlcInstallResult::kUnexpectedEmptyDlcId},
};

Metrics::DlcUninstallResultMap Metrics::uninstall_result_ = {
    {dlcservice::kErrorInvalidDlc,
     DlcUninstallResult::kDlcServiceReturnedInvalidDlc},  // dbus error
    {dlcservice::kErrorAllocation,
     DlcUninstallResult::kDlcServiceReturnedAllocation},  // dbus error
    {dlcservice::kErrorNoImageFound,
     DlcUninstallResult::kDlcServiceReturnedNoImageFound},  // dbus error
    {dlcservice::kErrorNeedReboot,
     DlcUninstallResult::kDlcServiceReturnedNeedReboot},  // dbus error
    {dlcservice::kErrorBusy,
     DlcUninstallResult::kDlcServiceReturnedBusy},  // dbus error
    {error::kDlcServiceReturnedErrorOnGetExistingDlcs,
     DlcUninstallResult::kDlcServiceReturnedErrorOnGetExistingDlcs},
    {error::kDlcServiceReturnedErrorOnPurge,
     DlcUninstallResult::kDlcServiceReturnedErrorOnPurge},
    {error::kUnexpectedEmptyVariant,
     DlcUninstallResult::kUnexpectedEmptyVariant},
};

// TODO(b/225970571): Add metrics for UpdateFirmwareCompleted

void Metrics::Init() {
  metrics_library_->Init();
}

void Metrics::SendDlcInstallResultSuccess() {
  SendDlcInstallResult(DlcInstallResult::kSuccess);
}

void Metrics::SendDlcInstallResultFailure(const brillo::Error* err) {
  DlcInstallResult res = DlcInstallResult::kUnknownError;
  DCHECK(err);
  // Iterate over all errors in the chain and get the deepest error that has a
  // match in the metrics map.
  const brillo::Error* err_it = err;
  while (err_it) {
    auto it = install_result_.find(err_it->GetCode());
    if (it != install_result_.end())
      res = it->second;
    err_it = err_it->GetInnerError();
  }
  SendDlcInstallResult(res);
}

void Metrics::SendDlcInstallResult(DlcInstallResult result) {
  ELOG(INFO) << "SendDlcInstallResult:" << static_cast<int>(result);
  metrics_library_->SendEnumToUMA(
      metrics::kMetricDlcInstallResult, static_cast<int>(result),
      static_cast<int>(DlcInstallResult::kNumConstants));
}

void Metrics::SendDlcUninstallResultSuccess() {
  SendDlcUninstallResult(DlcUninstallResult::kSuccess);
}

void Metrics::SendDlcUninstallResultFailure(const brillo::Error* err) {
  DlcUninstallResult res = DlcUninstallResult::kUnknownError;
  DCHECK(err);
  // Iterate over all errors in the chain and get the deepest error that has a
  // match in the metrics map.
  const brillo::Error* err_it = err;
  while (err_it) {
    auto it = uninstall_result_.find(err_it->GetCode());
    if (it != uninstall_result_.end())
      res = it->second;
    err_it = err_it->GetInnerError();
  }
  SendDlcUninstallResult(res);
}

void Metrics::SendDlcUninstallResult(DlcUninstallResult result) {
  ELOG(INFO) << "SendDlcUninstallResult:" << static_cast<int>(result);
  metrics_library_->SendEnumToUMA(
      metrics::kMetricDlcUninstallResult, static_cast<int>(result),
      static_cast<int>(DlcUninstallResult::kNumConstants));
}

// Sends the |FwUpdateLocation| value.
void Metrics::SendFwUpdateLocation(FwUpdateLocation location) {
  ELOG(INFO) << "SendFwUpdateLocation:" << static_cast<int>(location);
  metrics_library_->SendEnumToUMA(
      metrics::kMetricFwUpdateLocation, static_cast<int>(location),
      static_cast<int>(FwUpdateLocation::kNumConstants));
}

}  // namespace modemfwd
