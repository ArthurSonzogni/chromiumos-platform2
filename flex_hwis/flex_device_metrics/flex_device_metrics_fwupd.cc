// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_device_metrics/flex_device_metrics_fwupd.h"

#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include <base/containers/map_util.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>

namespace {

constexpr std::string_view kFwupdServiceName = "org.freedesktop.fwupd";
constexpr std::string_view kFwupdServicePath = "/";
constexpr std::string_view kFwupdInterface = "org.freedesktop.fwupd";
constexpr std::string_view kFwupdGetHistory = "GetHistory";

// Get the value of a field in `dict`.
//
// `nullopt` is returned if the key does not exist, or if the value of
// the field does not match `T`.
template <typename T>
std::optional<T> GetVarDictField(const brillo::VariantDictionary& dict,
                                 std::string_view key) {
  const brillo::Any* field = base::FindOrNull(dict, key);
  if (!field) {
    LOG(ERROR) << "Missing key: \"" << key << "\"";
    return std::nullopt;
  }

  T value;
  if (!field->GetValue(&value)) {
    LOG(ERROR) << "Value for key \"" << key << "\" has incorrect type";
    return std::nullopt;
  }

  return value;
}

// Parse a `FwupdRelease` from a `VariantDictionary`.
std::optional<FwupdRelease> ParseFwupdRelease(
    const brillo::VariantDictionary& raw_release) {
  // Note that the Metadata field is a string->string map, not a
  // VariantDictionary.
  const auto raw_metadata = GetVarDictField<std::map<std::string, std::string>>(
      raw_release, "Metadata");
  if (!raw_metadata.has_value()) {
    return std::nullopt;
  }

  const auto last_attempt_status =
      base::FindOrNull(raw_metadata.value(), "LastAttemptStatus");
  if (!last_attempt_status) {
    LOG(ERROR) << "Missing LastAttemptStatus field";
    return std::nullopt;
  }

  FwupdRelease release;
  if (!StringToAttemptStatus(*last_attempt_status,
                             &release.last_attempt_status)) {
    LOG(ERROR) << "Invalid FwupdLastAttemptStatus: " << *last_attempt_status;
    return std::nullopt;
  }

  return release;
}

// Parse a `FwupdDeviceHistory` from a `VariantDictionary`.
std::optional<FwupdDeviceHistory> ParseFwupdDeviceHistory(
    const brillo::VariantDictionary& raw_device) {
  const auto created = GetVarDictField<uint64_t>(raw_device, "Created");
  const auto name = GetVarDictField<std::string>(raw_device, "Name");
  const auto plugin = GetVarDictField<std::string>(raw_device, "Plugin");
  const auto raw_releases =
      GetVarDictField<std::vector<brillo::VariantDictionary>>(raw_device,
                                                              "Release");
  const auto update_state =
      GetVarDictField<uint32_t>(raw_device, "UpdateState");

  // Check that all expected fields exist.
  if (!created.has_value() || !name.has_value() || !plugin.has_value() ||
      !raw_releases.has_value() || !update_state.has_value()) {
    return std::nullopt;
  }

  // Validate the update state value.
  if (update_state.value() >
      static_cast<uint32_t>(FwupdUpdateState::kMaxValue)) {
    LOG(ERROR) << "Invalid FwupdUpdateState: " << update_state.value();
    return std::nullopt;
  }

  FwupdDeviceHistory device;
  device.name = name.value();
  device.plugin = plugin.value();
  device.created = base::Time::FromSecondsSinceUnixEpoch(created.value());
  device.update_state = static_cast<FwupdUpdateState>(update_state.value());

  // Parse releases.
  for (const auto& raw_release : raw_releases.value()) {
    const auto release = ParseFwupdRelease(raw_release);
    if (!release.has_value()) {
      return std::nullopt;
    }
    device.releases.push_back(release.value());
  }

  return device;
}

}  // namespace

bool StringToAttemptStatus(std::string_view s, FwupdLastAttemptStatus* result) {
  int status = 0;
  if (!base::HexStringToInt(s, &status)) {
    return false;
  }

  if (status >= static_cast<int>(FwupdLastAttemptStatus::kMinValue) &&
      status <= static_cast<int>(FwupdLastAttemptStatus::kMaxValue)) {
    *result = static_cast<FwupdLastAttemptStatus>(status);
    return true;
  }
  return false;
}

std::optional<std::vector<FwupdDeviceHistory>> ParseFwupdGetHistoryResponse(
    const std::vector<brillo::VariantDictionary>& raw_devices) {
  std::vector<FwupdDeviceHistory> devices;

  for (const auto& raw_device : raw_devices) {
    const auto device = ParseFwupdDeviceHistory(raw_device);
    if (!device) {
      return std::nullopt;
    }

    devices.push_back(device.value());
  }

  return devices;
}

std::optional<std::vector<FwupdDeviceHistory>> CallFwupdGetHistory(
    dbus::ObjectProxy* fwupd_proxy) {
  brillo::ErrorPtr error;
  auto resp = brillo::dbus_utils::CallMethodAndBlock(
      fwupd_proxy, std::string(kFwupdInterface), std::string(kFwupdGetHistory),
      &error);

  // Fwupd returns an error if there is no history.
  if (!resp && error->GetCode() == kFwupdGetHistoryNothingToDo) {
    return std::vector<FwupdDeviceHistory>();
  }

  std::vector<brillo::VariantDictionary> devices;
  if (resp && brillo::dbus_utils::ExtractMethodCallResults(resp.get(), &error,
                                                           &devices)) {
    return ParseFwupdGetHistoryResponse(devices);
  } else {
    LOG(ERROR) << "GetHistory call failed: " << error;
    return std::nullopt;
  }
}

std::optional<std::vector<FwupdDeviceHistory>> GetUpdateHistoryFromFwupd() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  CHECK(bus->Connect());
  dbus::ObjectProxy* fwupd_proxy = bus->GetObjectProxy(
      kFwupdServiceName, dbus::ObjectPath(kFwupdServicePath));

  return CallFwupdGetHistory(fwupd_proxy);
}

std::optional<base::Time> GetAndUpdateFwupMetricTimestamp(
    base::Time new_timestamp, const base::FilePath& path) {
  // Read the timestamp file.
  std::string time_str;
  const bool read_ok = base::ReadFileToString(base::FilePath(path), &time_str);

  // Update the timestamp file. Do this early, before any returns from
  // the function, to ensure we never skip updating the timestamp.
  if (!base::WriteFile(path, base::ToString(new_timestamp).append("\n"))) {
    PLOG(ERROR) << "Failed to write " << path;
    return std::nullopt;
  }

  // If the read failed, return a default value rather than an
  // error. It's expected that the timestamp file will not exist in some
  // cases (e.g. a fresh install or powerwash).
  if (!read_ok) {
    LOG(ERROR) << "Failed to read " << path;
    return base::Time::UnixEpoch();
  }

  // Trim the trailing newline from the file.
  base::Time time;
  base::TrimWhitespaceASCII(time_str, base::TRIM_TRAILING, &time_str);

  if (!base::Time::FromString(time_str.c_str(), &time)) {
    LOG(ERROR) << "Invalid timestamp: " << time_str;
    return std::nullopt;
  }

  return time;
}

std::optional<UpdateResult> AttemptStatusToUpdateResult(
    FwupdLastAttemptStatus status) {
  switch (status) {
    case FwupdLastAttemptStatus::kSuccess:
      return UpdateResult::kGenericFailure;
    case FwupdLastAttemptStatus::kErrorUnsuccessful:
      return UpdateResult::kErrorUnsuccessful;
    case FwupdLastAttemptStatus::kErrorInsufficientResources:
      return UpdateResult::kErrorInsufficientResources;
    case FwupdLastAttemptStatus::kErrorIncorrectVersion:
      return UpdateResult::kErrorIncorrectVersion;
    case FwupdLastAttemptStatus::kErrorInvalidFormat:
      return UpdateResult::kErrorInvalidFormat;
    case FwupdLastAttemptStatus::kErrorAuthError:
      return UpdateResult::kErrorAuthError;
    case FwupdLastAttemptStatus::kErrorPwrEvtAc:
      return UpdateResult::kErrorPwrEvtAc;
    case FwupdLastAttemptStatus::kErrorPwrEvtBatt:
      return UpdateResult::kErrorPwrEvtBatt;
    case FwupdLastAttemptStatus::kErrorUnsatisfiedDependencies:
      return UpdateResult::kErrorUnsatisfiedDependencies;
  }
  LOG(ERROR) << "Unexpected value for FwupdLastAttemptStatus: "
             << static_cast<int>(status);
  return std::nullopt;
}

std::optional<UpdateResult> UpdateStateToUpdateResult(FwupdUpdateState state) {
  switch (state) {
    case FwupdUpdateState::kUnknown:
      return UpdateResult::kUnknown;
    case FwupdUpdateState::kPending:
      return UpdateResult::kPending;
    case FwupdUpdateState::kSuccess:
      return UpdateResult::kSuccess;
    case FwupdUpdateState::kFailed:
      LOG(ERROR) << "No associated update result for kFailed update state.";
      return std::nullopt;
    case FwupdUpdateState::kNeedsReboot:
      return UpdateResult::kNeedsReboot;
    case FwupdUpdateState::kTransient:
      return UpdateResult::kTransient;
  }
  LOG(ERROR) << "Unexpected value for FwupdUpdateState "
             << static_cast<int>(state);
  return std::nullopt;
}

bool SendFwupMetric(MetricsLibraryInterface& metrics,
                    const FwupdDeviceHistory& history) {
  if (history.update_state == FwupdUpdateState::kFailed) {
    bool r = true;
    for (const auto& release : history.releases) {
      std::optional<UpdateResult> status =
          AttemptStatusToUpdateResult(release.last_attempt_status);
      if (!status.has_value() ||
          !metrics.SendEnumToUMA("Platform.FlexUefiCapsuleUpdateResult",
                                 status.value())) {
        LOG(ERROR)
            << "Failed to send FlexUefiCapsuleUpdateResult metric for device "
            << history.name;
        r = false;
      }
    }
    return r;
  } else {
    std::optional<UpdateResult> state =
        UpdateStateToUpdateResult(history.update_state);
    if (!state.has_value() ||
        !metrics.SendEnumToUMA("Platform.FlexUefiCapsuleUpdateResult",
                               state.value())) {
      LOG(ERROR)
          << "Failed to send FlexUefiCapsuleUpdateResult metric for device "
          << history.name;
      return false;
    } else {
      return true;
    }
  }
}

bool SendFwupMetrics(MetricsLibraryInterface& metrics,
                     const std::vector<FwupdDeviceHistory>& devices,
                     base::Time last_fwup_report) {
  bool all_success = true;
  for (const FwupdDeviceHistory& device : devices) {
    // Ignore non-UEFI updates.
    if (device.plugin != kUefiCapsulePlugin) {
      continue;
    }

    // Ignore updates older than the last-sent timestamp; UMAs for these
    // should already have been sent.
    if (device.created <= last_fwup_report) {
      continue;
    }

    if (!SendFwupMetric(metrics, device)) {
      all_success = false;
    }
  }
  return all_success;
}
