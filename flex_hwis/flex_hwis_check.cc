// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_check.h"

#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>

namespace flex_hwis {

namespace {
std::optional<std::string> ReadAndTrimFile(const base::FilePath& file_path) {
  std::string out;
  if (!base::ReadFileToString(file_path, &out))
    return std::nullopt;

  base::TrimWhitespaceASCII(out, base::TRIM_ALL, &out);
  return out;
}

bool CheckPolicy(const std::function<bool(bool*)> policy_member,
                 const std::string& log_name) {
  bool policy_permission = false;
  if (!policy_member(&policy_permission)) {
    LOG(INFO) << log_name << " is not set";
    return false;
  }
  if (!policy_permission) {
    LOG(INFO) << "Hardware data not sent: " << log_name << " disabled.";
    return false;
  }
  return true;
}

int64_t NowToEpochInSeconds() {
  return (base::Time::Now() - base::Time::UnixEpoch()).InSeconds();
}

}  // namespace

constexpr char kHwisUuidFile[] = "var/lib/flex_hwis_tool/uuid";
constexpr char kHwisTimeStampFile[] = "var/lib/flex_hwis_tool/time";

FlexHwisCheck::FlexHwisCheck(const base::FilePath& base_path,
                             policy::PolicyProvider& provider)
    : base_path_(base_path), policy_provider_(provider) {}

std::optional<std::string> FlexHwisCheck::GetUuid() const {
  return ReadHwisFile(UuidPath());
}

void FlexHwisCheck::DeleteUuid() {
  if (!brillo::DeleteFile(UuidPath())) {
    LOG(INFO) << "Error deleting UUID file";
  }
}

void FlexHwisCheck::SetUuid(const std::string_view uuid) {
  if (!WriteHwisFile(UuidPath(), uuid)) {
    LOG(INFO) << "Error writing UUID file";
  }
}

base::FilePath FlexHwisCheck::UuidPath() const {
  return base_path_.Append(kHwisUuidFile);
}

std::optional<std::string> FlexHwisCheck::ReadHwisFile(
    const base::FilePath& file_path) const {
  std::optional<std::string> hwis_info;

  if (!(hwis_info = ReadAndTrimFile(file_path))) {
    LOG(INFO) << "Couldn't read flex_hwis file.";
    return std::nullopt;
  }
  if (hwis_info.value().empty()) {
    LOG(INFO) << "Read a blank flex_hwis file.";
    return std::nullopt;
  }

  return hwis_info;
}

bool FlexHwisCheck::WriteHwisFile(const base::FilePath& file_path,
                                  const std::string_view content) {
  if (base::CreateDirectory(file_path.DirName())) {
    return base::ImportantFileWriter::WriteFileAtomically(
        file_path, std::string(content) + "\n");
  }
  return false;
}

bool FlexHwisCheck::HasRunRecently() {
  std::optional<std::string> last_str;
  const base::FilePath file_path = base_path_.Append(kHwisTimeStampFile);
  if ((last_str = ReadHwisFile(file_path))) {
    int64_t last_from_epoch = 0;
    if (base::StringToInt64(last_str.value(), &last_from_epoch)) {
      // The service must wait at least 24 hours between sending hardware data.
      if ((NowToEpochInSeconds() - last_from_epoch) <
          base::Days(1).InSeconds()) {
        return true;
      }
    } else {
      LOG(INFO) << "Failed to convert timestamp: " << last_str.value()
                << " to integer.";
    }
  }
  return false;
}

void FlexHwisCheck::RecordSendTime() {
  const base::FilePath file_path = base_path_.Append(kHwisTimeStampFile);
  if (!(WriteHwisFile(file_path,
                      base::NumberToString(NowToEpochInSeconds())))) {
    LOG(INFO) << "Failed to write the timestamp";
  }
}

PermissionInfo FlexHwisCheck::CheckPermission() {
  bool permission = true;
  PermissionInfo info;

  policy_provider_.Reload();
  if (!policy_provider_.device_policy_is_loaded()) {
    LOG(INFO) << "No device policy available on this device";
    return info;
  }
  info.loaded = true;

  const policy::DevicePolicy* policy = &policy_provider_.GetDevicePolicy();
  info.managed = policy->IsEnterpriseEnrolled();
  if (info.managed) {
    LOG(INFO) << "The device is managed";
    auto system_fn = std::bind(&policy::DevicePolicy::GetReportSystemInfo,
                               policy, std::placeholders::_1);
    auto cpu_fn = std::bind(&policy::DevicePolicy::GetReportCpuInfo, policy,
                            std::placeholders::_1);
    auto graphic_fn = std::bind(&policy::DevicePolicy::GetReportGraphicsStatus,
                                policy, std::placeholders::_1);
    auto memory_fn = std::bind(&policy::DevicePolicy::GetReportMemoryInfo,
                               policy, std::placeholders::_1);
    auto version_fn = std::bind(&policy::DevicePolicy::GetReportVersionInfo,
                                policy, std::placeholders::_1);
    auto network_fn = std::bind(&policy::DevicePolicy::GetReportNetworkConfig,
                                policy, std::placeholders::_1);
    permission = permission && CheckPolicy(system_fn, "DeviceSystemInfo");
    permission = permission && CheckPolicy(cpu_fn, "DeviceCpuInfo");
    permission = permission && CheckPolicy(graphic_fn, "DeviceGraphicsStatus");
    permission = permission && CheckPolicy(memory_fn, "DeviceMemoryInfo");
    permission = permission && CheckPolicy(version_fn, "DeviceVersionInfo");
    permission = permission && CheckPolicy(network_fn, "DeviceNetworkConfig");
  } else {
    LOG(INFO) << "The device is not managed";
    auto hw_data_fn = std::bind(&policy::DevicePolicy::GetHwDataUsageEnabled,
                                policy, std::placeholders::_1);
    permission = permission && CheckPolicy(hw_data_fn, "HardwareDataUsage");
  }
  info.permission = permission;
  return info;
}
}  // namespace flex_hwis
