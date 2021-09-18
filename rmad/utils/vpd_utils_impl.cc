// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/vpd_utils_impl.h"

#include <string>
#include <vector>

#include <base/process/launch.h>

namespace rmad {

namespace {

const char kVpdCmdPath[] = "/usr/sbin/vpd";

constexpr char kVpdKeySerialNumber[] = "serial_number";
constexpr char kVpdKeyWhitelabelTag[] = "whitelabel_tag";
constexpr char kVpdKeyRegion[] = "region";

}  // namespace

bool VpdUtilsImpl::GetSerialNumber(std::string* serial_number) const {
  CHECK(serial_number);

  return GetRoVpd(kVpdKeySerialNumber, serial_number);
}

bool VpdUtilsImpl::GetWhitelabelTag(std::string* whitelabel_tag) const {
  CHECK(whitelabel_tag);

  // We can allow whitelabel-tag to be empty.
  if (!GetRoVpd(kVpdKeyWhitelabelTag, whitelabel_tag)) {
    *whitelabel_tag = "";
  }

  return true;
}

bool VpdUtilsImpl::GetRegion(std::string* region) const {
  CHECK(region);

  return GetRoVpd(kVpdKeyRegion, region);
}

bool VpdUtilsImpl::GetCalibbias(const std::vector<std::string>& entries,
                                std::vector<int>* calibbias) const {
  CHECK(calibbias);

  std::vector<int> values;
  for (const std::string& entry : entries) {
    std::string str;
    int val;
    if (!GetRoVpd(entry, &str) || !base::StringToInt(str, &val)) {
      LOG(ERROR) << "Failed to get int value of " << entry << " from vpd.";
      return false;
    }
    values.push_back(val);
  }

  *calibbias = values;

  return true;
}

bool VpdUtilsImpl::SetSerialNumber(const std::string& serial_number) {
  return SetRoVpd(kVpdKeySerialNumber, serial_number);
}

bool VpdUtilsImpl::SetWhitelabelTag(const std::string& whitelabel_tag) {
  return SetRoVpd(kVpdKeyWhitelabelTag, whitelabel_tag);
}

bool VpdUtilsImpl::SetRegion(const std::string& region) {
  return SetRoVpd(kVpdKeyRegion, region);
}

bool VpdUtilsImpl::SetCalibbias(const std::vector<std::string>& entries,
                                const std::vector<int>& calibbias) {
  CHECK(calibbias.size() == entries.size());

  for (int i = 0; i < calibbias.size(); i++) {
    std::string str = base::NumberToString(calibbias[i]);
    if (!SetRoVpd(entries[i], str)) {
      LOG(ERROR) << "Failed to set " << entries[i] << "=" << str << " to vpd.";
      return false;
    }
  }

  return true;
}

bool VpdUtilsImpl::SetRoVpd(const std::string& key, const std::string& value) {
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RO_VPD", "-s",
                                key + "=" + value};
  static std::string unused_output;
  return base::GetAppOutput(argv, &unused_output);
}

bool VpdUtilsImpl::GetRoVpd(const std::string& key, std::string* value) const {
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RO_VPD", "-g", key};
  return base::GetAppOutput(argv, value);
}

bool VpdUtilsImpl::SetRwVpd(const std::string& key, const std::string& value) {
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RW_VPD", "-s",
                                key + "=" + value};
  static std::string unused_output;
  return base::GetAppOutput(argv, &unused_output);
}

bool VpdUtilsImpl::GetRwVpd(const std::string& key, std::string* value) const {
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RW_VPD", "-g", key};
  return base::GetAppOutput(argv, value);
}

}  // namespace rmad
