// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_METRICS_CONSTANTS_H_
#define UPDATE_ENGINE_COMMON_METRICS_CONSTANTS_H_

namespace chromeos_update_engine {

namespace metrics {
// The possible outcomes when checking for updates.
//
// This is used in the UpdateEngine.Check.Result histogram.
enum class CheckResult {
  kUpdateAvailable,    // Response indicates an update is available.
  kNoUpdateAvailable,  // Response indicates no updates are available.
  kDownloadError,      // Error downloading response from Omaha.
  kParsingError,       // Error parsing response.
  kRebootPending,      // No update check was performed a reboot is pending.
  kDeferredUpdate,     // Update is applied, but deferred.

  kNumConstants,
  kUnset = -1
};

// Possible ways a device can react to a new update being available.
//
// This is used in the UpdateEngine.Check.Reaction histogram.
enum class CheckReaction {
  kUpdating,    // Device proceeds to download and apply update.
  kIgnored,     // Device-policy dictates ignoring the update.
  kDeferring,   // Device-policy dictates waiting.
  kBackingOff,  // Previous errors dictates waiting.

  kNumConstants,
  kUnset = -1
};

// The possible ways that downloading from a HTTP or HTTPS server can fail.
//
// This is used in the UpdateEngine.Check.DownloadErrorCode and
// UpdateEngine.Attempt.DownloadErrorCode histograms.
enum class DownloadErrorCode {
  // Errors that can happen in the field. See http://crbug.com/355745
  // for how we plan to add more detail in the future.
  kDownloadError = 0,  // Error downloading data from server.

  // IMPORTANT: When adding a new error code, add at the bottom of the
  // above block and before the kInputMalformed field. This
  // is to ensure that error codes are not reordered.

  // This error is reported when libcurl returns CURLE_COULDNT_RESOLVE_HOST and
  // calling res_init() can recover.
  kUnresolvedHostRecovered = 97,
  // This error is reported when libcurl returns CURLE_COULDNT_RESOLVE_HOST.
  kUnresolvedHostError = 98,
  // This error is reported when libcurl has an internal error that
  // update_engine can't recover from.
  kInternalLibCurlError = 99,

  // This error code is used to convey that malformed input was given
  // to the utils::GetDownloadErrorCode() function. This should never
  // happen but if it does it's because of an internal update_engine
  // error and we're interested in knowing this.
  kInputMalformed = 100,

  // Bucket for capturing HTTP status codes not in the 200-599
  // range. This should never happen in practice but if it does we
  // want to know.
  kHttpStatusOther = 101,

  // Above 200 and below 600, the value is the HTTP status code.
  kHttpStatus200 = 200,

  kNumConstants = 600,

  kUnset = -1
};

// Possible ways an update attempt can end.
//
// This is used in the UpdateEngine.Attempt.Result histogram.
enum class AttemptResult {
  kUpdateSucceeded,             // The update succeeded.
  kInternalError,               // An internal error occurred.
  kPayloadDownloadError,        // Failure while downloading payload.
  kMetadataMalformed,           // Metadata was malformed.
  kOperationMalformed,          // An operation was malformed.
  kOperationExecutionError,     // An operation failed to execute.
  kMetadataVerificationFailed,  // Metadata verification failed.
  kPayloadVerificationFailed,   // Payload verification failed.
  kVerificationFailed,          // Root or Kernel partition verification failed.
  kPostInstallFailed,           // The postinstall step failed.
  kAbnormalTermination,         // The attempt ended abnormally.
  kUpdateCanceled,              // Update canceled by the user.
  kUpdateSucceededNotActive,    // Update succeeded but the new slot is not
                                // active.
  kUpdateSkipped,               // Current update skipped.
  kNumConstants,

  kUnset = -1
};

// Possible ways the device is connected to the Internet.
//
// This is used in the UpdateEngine.Attempt.ConnectionType histogram.
enum class ConnectionType {
  kUnknown = 0,            // Unknown.
  kEthernet = 1,           // Ethernet (unmetered by default).
  kWifi = 2,               // Wireless (unmetered by default).
  kCellular = 5,           // Cellular (metered by default).
  kDisconnected = 8,       // Disconnected.
  kUnmeteredCellular = 9,  // Cellular (unmetered).
  kMeteredWifi = 10,       // Wireless (metered).
  // deprecated: kTetheredEthernet = 6,
  // deprecated: kTetheredWifi = 7,
  // deprecated: kWimax = 3,
  // deprecated: kBluetooth = 4,

  kNumConstants,
  kUnset = -1
};

// Possible ways a rollback can end.
//
// This is used in the UpdateEngine.Rollback histogram.
enum class RollbackResult {
  kFailed,
  kSuccess,

  kNumConstants
};

}  // namespace metrics

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_METRICS_CONSTANTS_H_
