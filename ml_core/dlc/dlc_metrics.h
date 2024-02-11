// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_CORE_DLC_DLC_METRICS_H_
#define ML_CORE_DLC_DLC_METRICS_H_

#include <string>

#include <metrics/metrics_library.h>

namespace cros {

// NOTE:
// Enums in this file are persisted to logs. Entries should not be renumbered
// and numeric values should never be reused.

// One entry for each kError{...} error code in
// third_party/cros_system_api/dbus/dlcservice/dbus-constants.h, plus one entry
// for an unrecognized error code.
//
// Keep this in sync with enum "MachineLearningServiceDlcErrorCode" in
// tools/metrics/histograms/metadata/cros_ml/enums.xml.
enum class DlcErrorCode {
  kUnrecognized = 0,
  kNone = 1,
  kInternal = 2,
  kBusy = 3,
  kNeedReboot = 4,
  kInvalidDlc = 5,
  kAllocation = 6,
  kNoImageFound = 7,
  kMaxValue = kNoImageFound
};

// Map DLC error code string in
// third_party/cros_system_api/dbus/dlcservice/dbus-constants.h to a
// DlcErrorCode.
DlcErrorCode DlcErrorCodeEnumFromString(std::string_view error);

// Result of an attempt to request DLC Service to begin installing the DLC.
//
// Keep this in sync with enum "MachineLearningServiceDlcBeginInstallResult" in
// tools/metrics/histograms/metadata/cros_ml/enums.xml.
enum class DlcBeginInstallResult {
  kSuccess = 0,
  kDBusNotConnected = 1,
  kDlcServiceBusyWillAbort = 2,
  kDlcServiceBusyWillRetry = 3,
  kOtherDlcServiceError = 4,
  kUnknownDlcServiceFailure = 5,
  kMaxValue = kUnknownDlcServiceFailure
};

// Final result of an attempt to install a DLC.
//
// Keep this in sync with enum "MachineLearningServiceDlcFinalInstallResult" in
// tools/metrics/histograms/metadata/cros_ml/enums.xml.
enum class DlcFinalInstallResult {
  kSuccess = 0,
  kDlcServiceError = 1,
  kMaxValue = kDlcServiceError
};

// Methods for recording DLC-related metrics & events.
class DlcMetrics {
 public:
  DlcMetrics();
  ~DlcMetrics();

  // Set the base name for emitted histograms.
  void SetMetricsBaseName(const std::string& metrics_base_name);

  // Record the result of a single attempt to begin an install via D-Bus.
  void RecordBeginInstallResult(DlcBeginInstallResult result);

  // Record the error code received from DLC Service while trying to begin
  // install of the DLC.
  void RecordBeginInstallDlcServiceError(DlcErrorCode error);

  // Record the final installation outcome for the DLC.
  void RecordFinalInstallResult(DlcFinalInstallResult result);

  // Record the final error code received from DLC Service after a DLC
  // installation request.
  void RecordFinalInstallDlcServiceError(DlcErrorCode error);

  // Record that attempt number `n` is being made to install the DLC out of a
  // maximum of `max` attempts.
  void RecordInstallAttemptCount(int n, int max);

 private:
  std::string metrics_base_name_;
  MetricsLibrary metrics_library_;
};

}  // namespace cros

#endif  // ML_CORE_DLC_DLC_METRICS_H_
