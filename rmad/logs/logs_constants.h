// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_LOGS_LOGS_CONSTANTS_H_
#define RMAD_LOGS_LOGS_CONSTANTS_H_

#include <base/values.h>

namespace rmad {

// JsonStore keys.
inline constexpr char kLogs[] = "logs";
inline constexpr char kEvents[] = "events";

// Event keys.
inline constexpr char kTimestamp[] = "timestamp";
inline constexpr char kStateId[] = "state_id";
inline constexpr char kType[] = "type";
inline constexpr char kDetails[] = "details";

// State transition keys.
inline constexpr char kFromStateId[] = "from_state_id";
inline constexpr char kToStateId[] = "to_state_id";

// Error keys.
inline constexpr char kOccurredError[] = "occurred_error";

// State specific attributes.
inline constexpr char kLogReplacedComponents[] = "replaced_components";
inline constexpr char kLogReworkSelected[] = "rework_selected";
inline constexpr char kLogDestination[] = "destination";
inline constexpr char kLogWipeDevice[] = "wipe_device";
inline constexpr char kLogWpDisableMethod[] = "wp_disable_method";
inline constexpr char kLogRsuChallengeCode[] = "challenge_code";
inline constexpr char kLogRsuHwid[] = "hwid";
inline constexpr char kLogRestockOption[] = "restock_option";
inline constexpr char kLogCalibrationComponents[] = "calibration_components";
inline constexpr char kLogComponent[] = "component";
inline constexpr char kLogCalibrationStatus[] = "calibration_status";
inline constexpr char kFirmwareStatus[] = "firmware_status";

// Log string formats.
constexpr char kLogTimestampFormat[] = "[%04d-%02d-%02d %02d:%02d:%02d] ";
constexpr char kLogTransitionFormat[] = "Transitioned from %s to %s\n";
constexpr char kLogErrorFormat[] = "ERROR in %s: %s\n";

enum class LogEventType {
  kTransition = 0,
  kData = 1,
  kError = 2,
  kMaxValue = kError,
};

enum class LogCalibrationStatus {
  kFailed = 0,
  kSkip = 1,
  kRetry = 2,
  kMaxValue = kRetry,
};

enum class FirmwareUpdateStatus {
  kUsbPluggedIn = 0,
  kUsbPluggedInFileNotFound = 1,
  kFirmwareUpdated = 2,
  kFirmwareComplete = 3,
  kMaxValue = kFirmwareComplete,
};

}  // namespace rmad

#endif  // RMAD_LOGS_LOGS_CONSTANTS_H_
