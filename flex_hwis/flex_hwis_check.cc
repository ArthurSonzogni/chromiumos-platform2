// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_check.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>

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

}  // namespace

constexpr char kKernelUuidFile[] = "proc/sys/kernel/random/uuid";
constexpr char kHwisUuidFile[] = "var/lib/flex_hwis_tool/uuid";
constexpr char kHwisTimeStampFile[] = "var/lib/flex_hwis_tool/time";

FlexHwisCheck::FlexHwisCheck(const base::FilePath& base_path,
                             std::unique_ptr<policy::PolicyProvider> provider)
    : base_path_(base_path), policy_provider_(std::move(provider)) {}

UuidInfo FlexHwisCheck::GetOrCreateUuid() {
  UuidInfo info;
  const base::FilePath hwis_uuid_path = base_path_.Append(kHwisUuidFile);
  const base::FilePath kernel_uuid_path = base_path_.Append(kKernelUuidFile);

  if ((info.uuid = ReadHwisFile(hwis_uuid_path))) {
    LOG(INFO) << "UUID has already been generated";
    info.already_exists = true;
    return info;
  }

  if (!(info.uuid = ReadHwisFile(kernel_uuid_path))) {
    LOG(INFO) << "Error reading kernel UUID";
    return info;
  }

  if (!WriteHwisFile(hwis_uuid_path, info.uuid.value())) {
    LOG(INFO) << "Error writing UUID file";
    return info;
  }

  LOG(INFO) << "Successfully wrote uuid: " << info.uuid.value();
  return info;
}

std::optional<std::string> FlexHwisCheck::ReadHwisFile(
    const base::FilePath& file_path) {
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
                                  const std::string& content) {
  if (base::CreateDirectory(file_path.DirName())) {
    return base::ImportantFileWriter::WriteFileAtomically(file_path,
                                                          content + "\n");
  }
  return false;
}

bool FlexHwisCheck::HasRunRecently() {
  base::Time current = base::Time::Now(), last;
  std::optional<std::string> last_str;
  const base::FilePath file_path = base_path_.Append(kHwisTimeStampFile);
  if ((last_str = ReadHwisFile(file_path))) {
    if (base::Time::FromString(last_str.value().c_str(), &last)) {
      // The service must wait at least 24 hours between sending hardware data.
      if ((current - last) < base::Days(1)) {
        return true;
      }
    }
  }
  return false;
}

void FlexHwisCheck::RecordSendTime() {
  base::Time current = base::Time::Now();
  const base::FilePath file_path = base_path_.Append(kHwisTimeStampFile);
  if (!(WriteHwisFile(file_path, base::TimeFormatHTTP(current)))) {
    LOG(INFO) << "Failed to write the timestamp";
  }
}

bool FlexHwisCheck::CheckPermission() {
  bool permission = true;

  policy_provider_->Reload();
  if (!policy_provider_->device_policy_is_loaded()) {
    LOG(INFO) << "No device policy available on this device";
    return false;
  }

  const policy::DevicePolicy* policy = &policy_provider_->GetDevicePolicy();

  if (policy->IsEnterpriseEnrolled()) {
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

  return permission;
}
}  // namespace flex_hwis
