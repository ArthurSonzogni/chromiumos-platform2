// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/metrics/enterprise_rollback_metrics_tracking.h"

#include <optional>
#include <string>
#include <vector>

#include <base/strings/strcat.h>
#include <base/strings/string_split.h>
#include <base/system/sys_info.h>
#include <base/version.h>

namespace oobe_config {

namespace {

constexpr char kLsbReleaseVersionKey[] = "CHROMEOS_RELEASE_VERSION";

// TargetVersionPrefix policy is expected to be received with format "<major>.".
// However, it could be set with other values: "<major>.*", "<major>.<minor>.*"
// or "<major>.<minor>.<patch>". Rollback metrics require a Version with three
// values. For simplicity, independently of the value of the policy, we track
// target version as "<major>.0.0".
std::optional<base::Version> ConvertPolicyToMajorVersion(
    std::string target_version_policy) {
  std::vector<std::string_view> version_numbers = base::SplitStringPiece(
      target_version_policy, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (version_numbers.empty()) {
    return std::nullopt;
  }

  base::Version target_version(base::StrCat({version_numbers[0], ".0.0"}));
  if (!target_version.IsValid()) {
    return std::nullopt;
  }
  return target_version;
}

// Starts a new enterprise rollback tracking for `target_version`.
bool RollbackPolicyActivatedStartTracking(
    oobe_config::EnterpriseRollbackMetricsHandler& rolback_metrics,
    base::Version device_version,
    base::Version target_version) {
  if (!rolback_metrics.StartTrackingRollback(device_version, target_version)) {
    return false;
  }

  rolback_metrics.TrackEvent(
      oobe_config::EnterpriseRollbackMetricsHandler::CreateEventData(
          EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  return true;
}

}  // namespace

std::optional<base::Version> GetDeviceVersion() {
  std::string version;
  if (!base::SysInfo::GetLsbReleaseValue(kLsbReleaseVersionKey, &version)) {
    return std::nullopt;
  }
  base::Version device_version(version);
  if (!device_version.IsValid()) {
    return std::nullopt;
  }

  return device_version;
}

bool CleanOutdatedTracking(
    oobe_config::EnterpriseRollbackMetricsHandler& rolback_metrics) {
  if (rolback_metrics.IsTrackingRollback()) {
    return rolback_metrics.StopTrackingRollback();
  }

  return true;
}

base::expected<bool, std::string> IsTrackingForRollbackTargetVersion(
    oobe_config::EnterpriseRollbackMetricsHandler& rolback_metrics,
    std::string target_version_policy) {
  if (!rolback_metrics.IsTrackingRollback()) {
    return false;
  }

  std::optional<base::Version> target_version =
      ConvertPolicyToMajorVersion(target_version_policy);
  if (!target_version.has_value()) {
    return base::unexpected("Error converting target version policy");
  }

  return rolback_metrics.IsTrackingForTargetVersion(*target_version);
}

bool StartNewTracking(
    oobe_config::EnterpriseRollbackMetricsHandler& rolback_metrics,
    std::string target_version_policy) {
  std::optional<base::Version> target_version =
      ConvertPolicyToMajorVersion(target_version_policy);
  if (!target_version.has_value()) {
    LOG(INFO) << "Error converting target version policy";
    return false;
  }

  std::optional<base::Version> device_version = GetDeviceVersion();
  if (!device_version.has_value()) {
    LOG(ERROR) << "Error reading ChromeOS version";
    return false;
  }

  return RollbackPolicyActivatedStartTracking(rolback_metrics, *device_version,
                                              *target_version);
}

}  // namespace oobe_config
