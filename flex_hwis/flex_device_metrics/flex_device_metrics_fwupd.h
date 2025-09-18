// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_DEVICE_METRICS_FLEX_DEVICE_METRICS_FWUPD_H_
#define FLEX_HWIS_FLEX_DEVICE_METRICS_FLEX_DEVICE_METRICS_FWUPD_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <brillo/variant_dictionary.h>
#include <metrics/metrics_library.h>

namespace dbus {
class ObjectProxy;
}

// Error returned by fwupd's GetHistory method when the history is empty.
constexpr std::string_view kFwupdGetHistoryNothingToDo =
    "org.freedesktop.fwupd.NothingToDo";

// Filepath to record the last time fwup history metrics were sent.
inline constexpr std::string_view kFwupTimestampFile =
    "/var/lib/flex_device_metrics/last_fwup_report";

// The string representing the UEFI capsule [1] plugin [2] for fwupd.
// [1]: https://github.com/fwupd/fwupd/tree/main/plugins/uefi-capsule
// [2]: https://fwupd.github.io/libfwupdplugin
inline constexpr std::string_view kUefiCapsulePlugin = "uefi_capsule";

// Enum representing the fwupd update state as defined in
// https://github.com/fwupd/fwupd/blob/240e65e92e53ead489a3ecdff668d6b4eea340fc/libfwupd/fwupd-enums.h#L1185
enum class FwupdUpdateState {
  // Unknown.
  kUnknown = 0,
  // Update is pending.
  kPending = 1,
  // Update was successful.
  kSuccess = 2,
  // Update failed.
  kFailed = 3,
  // Waiting for a reboot to apply.
  kNeedsReboot = 4,
  // Update failed due to transient issue, e.g. AC power required.
  kTransient = 5,

  kMaxValue = kTransient,
};

// The capsule device status [1] resulting from the last update attempt.
// This can provide a more specific failure reason in the case of update
// failure.
//
// [1]:
// https://uefi.org/specs/UEFI/2.11/23_Firmware_Update_and_Reporting.html#id30
enum class FwupdLastAttemptStatus {
  // Update was successful.
  kSuccess = 0,
  // Update was unsuccessful.
  kErrorUnsuccessful = 1,
  // There were insufficient resources to process the capsule.
  kErrorInsufficientResources = 2,
  // Version mismatch.
  kErrorIncorrectVersion = 3,
  // Firmware had invalid format.
  kErrorInvalidFormat = 4,
  // Authentication signing error.
  kErrorAuthError = 5,
  // AC power was not connected during update.
  kErrorPwrEvtAc = 6,
  // Battery level is too low.
  kErrorPwrEvtBatt = 7,
  // Unsatisfied Dependencies.
  kErrorUnsatisfiedDependencies = 8,

  kMinValue = kSuccess,
  kMaxValue = kErrorUnsatisfiedDependencies,
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class UpdateResult {
  // The following correlate to the `FwupdUpdateState` enum.

  // Unknown.
  kUnknown = 0,

  // Update is pending.
  kPending = 1,
  // Update was successful.
  kSuccess = 2,
  // Waiting for a reboot to apply update.
  kNeedsReboot = 3,
  // Update failed due to transient issue, e.g. AC power required.
  kTransient = 4,

  // The following correlate to the `FwupdLastAttemptStatus` enum.
  // They represent possible causes for a `kFailed` update state.

  // Firmware version does not match expected version, but the plugin does not
  // know what specifically went wrong.
  kGenericFailure = 5,
  // Update was unsuccessful.
  kErrorUnsuccessful = 6,
  // There were insufficient resources to process the capsule.
  kErrorInsufficientResources = 7,
  // Version mismatch.
  kErrorIncorrectVersion = 8,
  // Firmware had invalid format.
  kErrorInvalidFormat = 9,
  // Authentication signing error.
  kErrorAuthError = 10,
  // AC power was not connected during update.
  kErrorPwrEvtAc = 11,
  // Battery level is too low.
  kErrorPwrEvtBatt = 12,
  // Unsatisfied Dependencies.
  kErrorUnsatisfiedDependencies = 13,

  kMaxValue = kErrorUnsatisfiedDependencies,
};

// Struct containing the only field we are interested in from
// the `Release` json object contained in the fwupd history response:
// the last attempt status.
struct FwupdRelease {
  bool operator==(const FwupdRelease&) const = default;

  FwupdLastAttemptStatus last_attempt_status;
};

// The `Device` struct within fwupd's json response
// contains many more fields than those listed below,
// however we only convert the fields we need.
struct FwupdDeviceHistory {
  bool operator==(const FwupdDeviceHistory&) const = default;

  // Device name.
  std::string name;
  // The fwupd plugin, used to check whether the update was installed
  // with a UEFI plugin.
  std::string plugin;
  // The time when the history entry for the device was last modified.
  base::Time modified;
  // Update state, a per device value.
  FwupdUpdateState update_state;
  // The list of `Release` struct, each containing a `FwupdLastAttemptStatus`
  // which can narrow down failure reasons.
  std::vector<FwupdRelease> releases;
};

// Get the timestamp stored in `path`, and also update the file to
// contain `new_timestamp`.
//
// If the file does not exist, `base::Time::UnixEpoch()` is
// returned. (The file will not exist until the first time metrics are
// sent, so this case is not handled as an error.)
//
// If the contents of the file are invalid, or if the file cannot be
// updated, `nullopt` is returned.
std::optional<base::Time> GetAndUpdateFwupMetricTimestamp(
    base::Time new_timestamp,
    const base::FilePath& path = base::FilePath(kFwupTimestampFile));

// Convert a string to a `FwupdLastAttemptStatus`, returning
// `std::nullopt` in case of error.
bool StringToAttemptStatus(std::string_view s, FwupdLastAttemptStatus* result);

// Convert `FwupdLastAttemptStatus` into its associated `UpdateResult`,
// returning `std::nullopt` in case of error.
std::optional<UpdateResult> AttemptStatusToUpdateResult(
    FwupdLastAttemptStatus status);

// Convert `FwupdUpdateState` into its associated `UpdateResult`,
// returning `std::nullopt` in case of error.
std::optional<UpdateResult> UpdateStateToUpdateResult(FwupdUpdateState state);

// Parse a vector of `FwupdDeviceHistory` from a vector of
// `VariantDictionary`. This is used to convert raw dbus data to a more
// useful format.
std::optional<std::vector<FwupdDeviceHistory>> ParseFwupdGetHistoryResponse(
    const std::vector<brillo::VariantDictionary>& raw_devices);

// Call fwupd's `GetHistory` dbus method on the provided `fwupd_proxy`,
// and return the results.
//
// If there are no updates in the history, an empty vector is
// returned. If the dbus call fails, or if the response cannot be
// parsed, `nullopt` is returned.
std::optional<std::vector<FwupdDeviceHistory>> CallFwupdGetHistory(
    dbus::ObjectProxy* fwupd_proxy);

// Call fwupd's `GetHistory` dbus method and return the results.
//
// If there are no updates in the history, an empty vector is
// returned. If the dbus call fails, or if the response cannot be
// parsed, `nullopt` is returned.
std::optional<std::vector<FwupdDeviceHistory>> GetUpdateHistoryFromFwupd();

// Send the Firmware Update Result metric.
//
// This is an enum metric, see `UpdateResult`.
//
// For failed updates, a metric will be sent for each release.
// The program will not exit early if one release was not successfully
// sent, however it will return false.
//
// Returns true if all metrics were sent successfully,
// false if any error occurs.
bool SendFwupMetric(MetricsLibraryInterface& metrics,
                    const FwupdDeviceHistory& history);

// Send the status of each update history as a UMA.
//
// Any updates that do not use the `uefi_capsule` plugin
// or were created before the last time metrics were sent will
// be skipped.
//
// Returns true if all metrics were sent successfully,
// false if any error occurs.
bool SendFwupMetrics(MetricsLibraryInterface& metrics,
                     const std::vector<FwupdDeviceHistory>& devices,
                     base::Time last_fwup_report);

#endif  // FLEX_HWIS_FLEX_DEVICE_METRICS_FLEX_DEVICE_METRICS_FWUPD_H_
