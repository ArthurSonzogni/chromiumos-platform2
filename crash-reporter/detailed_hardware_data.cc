// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/detailed_hardware_data.h"

#include <map>
#include <optional>
#include <string>
#include <utility>

#include <chromeos/constants/flex_hwis.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "crash-reporter/paths.h"

// All the files under /sys/class/dmi/id/ appear to be 4096 bytes, though the
// actual contents are smaller. I can't find where in the spec it says how big
// DMI fields can be, but it looks like the kernel "dmi_header" uses a u8 to
// store the length.
constexpr int64_t kDmiMaxSize = 256;
constexpr char kProductNameKey[] = "chromeosflex_product_name";
constexpr char kProductVersionKey[] = "chromeosflex_product_version";
// This string is intentionally different to match the field as used elsewhere.
constexpr char kSysVendorKey[] = "chromeosflex_product_vendor";

// The longest component string in rubber-chicken is 195 chars long.
// Leave some extra space, but the long ones are names meant for humans to read
// so truncation isn't likely to cause problems.
constexpr int64_t kHardwareComponentMaxSize = 256;

namespace detailed_hardware_data {

namespace {

std::optional<std::string> ReadDmiIdBestEffort(const std::string& file) {
  const base::FilePath path = paths::Get(paths::kDmiIdDirectory).Append(file);

  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents, kDmiMaxSize)) {
    LOG(INFO) << "Couldn't read " << path.value();
    return std::nullopt;
  }

  // The kernel adds a trailing newline to the DMI files it
  // exposes. Trim that character, but don't trim any other trailing
  // whitespace as that would be in the DMI data itself.
  if (base::EndsWith(contents, "\n")) {
    contents.pop_back();
  }

  return contents;
}

}  // namespace

std::map<std::string, std::string> DmiModelInfo() {
  const std::optional<std::string> product_name(
      ReadDmiIdBestEffort(paths::kProductNameFile));
  const std::optional<std::string> product_version(
      ReadDmiIdBestEffort(paths::kProductVersionFile));
  const std::optional<std::string> sys_vendor(
      ReadDmiIdBestEffort(paths::kSysVendorFile));

  std::map<std::string, std::string> result;
  // For these three we really care about the distinction between not having
  // read it and having read it but the file being empty -- some OEMs put/leave
  // "useless" values (like empty strings or "To be filled by O.E.M.") in there,
  // but even those can provide some signal.
  if (product_name.has_value()) {
    result.emplace(kProductNameKey, product_name.value());
  }
  if (product_version.has_value()) {
    result.emplace(kProductVersionKey, product_version.value());
  }
  if (sys_vendor.has_value()) {
    result.emplace(kSysVendorKey, sys_vendor.value());
  }

  return result;
}

std::map<std::string, std::string> FlexComponentInfo() {
  const base::FilePath hardware_cache_dir =
      paths::Get(flex_hwis::kFlexHardwareCacheDir);

  // This is a subset of what's sent for feedback:
  // https://source.chromium.org/chromium/chromium/src/+/main:chrome/browser/ash/system_logs/reven_log_source.cc;l=29-62;drc=a415a6b0254c3843cdf3ccce2fb54808fb8e1c6b
  auto hardware_component_keys = {
      flex_hwis::kFlexBiosVersionKey,   flex_hwis::kFlexCpuNameKey,
      flex_hwis::kFlexEthernetIdKey,    flex_hwis::kFlexEthernetNameKey,
      flex_hwis::kFlexWirelessIdKey,    flex_hwis::kFlexWirelessNameKey,
      flex_hwis::kFlexBluetoothIdKey,   flex_hwis::kFlexBluetoothNameKey,
      flex_hwis::kFlexGpuIdKey,         flex_hwis::kFlexGpuNameKey,
      flex_hwis::kFlexTouchpadStackKey, flex_hwis::kFlexTpmVersionKey,
      flex_hwis::kFlexTpmSpecLevelKey,  flex_hwis::kFlexTpmManufacturerKey,
  };

  std::map<std::string, std::string> result;
  for (const std::string_view key : hardware_component_keys) {
    const base::FilePath file = hardware_cache_dir.Append(key);
    std::string content;
    const bool fully_read = base::ReadFileToStringWithMaxSize(
        file, &content, kHardwareComponentMaxSize);
    if (!fully_read) {
      // Don't send partial reads to keep things simple when interpreting data.
      // This case should be rare enough that it's not worth making people think
      // about "does bios_version `1.2<partial read>` match `1.21` or `1.23`?"
      LOG(WARNING) << "Failed to read " << key << ". Got: " << content;
      continue;
    }

    result.emplace(key, std::move(content));
  }

  return result;
}

bool FlexComponentInfoAllowedByPolicy(
    const policy::DevicePolicy& device_policy) {
  const bool is_enterprise_enrolled(device_policy.IsEnterpriseEnrolled());

  std::optional<bool> allowed;
  if (is_enterprise_enrolled) {
    allowed = device_policy.GetEnrolledHwDataUsageEnabled();
  } else {
    allowed = device_policy.GetUnenrolledHwDataUsageEnabled();
  }

  if (!allowed.has_value()) {
    LOG(INFO) << "Couldn't read policy for detailed hardware data.";
    return false;
  }

  return *allowed;
}

}  // namespace detailed_hardware_data
