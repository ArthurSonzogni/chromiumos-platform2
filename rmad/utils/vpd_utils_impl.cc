// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/vpd_utils_impl.h"

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/process/launch.h>

namespace rmad {

namespace {

const char kVpdCmdPath[] = "/usr/sbin/vpd";

constexpr char kVpdKeySerialNumber[] = "serial_number";
constexpr char kVpdKeyWhitelabelTag[] = "whitelabel_tag";
constexpr char kVpdKeyRegion[] = "region";

}  // namespace

// We flush all caches into
VpdUtilsImpl::~VpdUtilsImpl() {
  FlushOutRoVpdCache();
  FlushOutRwVpdCache();
}

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
  cache_ro_[kVpdKeySerialNumber] = serial_number;
  return true;
}

bool VpdUtilsImpl::SetWhitelabelTag(const std::string& whitelabel_tag) {
  cache_ro_[kVpdKeyWhitelabelTag] = whitelabel_tag;
  return true;
}

bool VpdUtilsImpl::SetRegion(const std::string& region) {
  cache_ro_[kVpdKeyRegion] = region;
  return true;
}

bool VpdUtilsImpl::SetCalibbias(const std::map<std::string, int>& calibbias) {
  for (const auto& [key, value] : calibbias) {
    cache_ro_[key] = base::NumberToString(value);
  }

  return true;
}

bool VpdUtilsImpl::FlushOutRoVpdCache() {
  if (cache_ro_.size() && !SetRoVpd(cache_ro_)) {
    return false;
  }

  cache_ro_.clear();
  return true;
}

bool VpdUtilsImpl::FlushOutRwVpdCache() {
  if (cache_rw_.size() && !SetRwVpd(cache_rw_)) {
    return false;
  }

  cache_rw_.clear();
  return true;
}

bool VpdUtilsImpl::SetRoVpd(
    const std::map<std::string, std::string>& key_value_map) {
  std::string log_msg;
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RO_VPD"};
  for (const auto& [key, value] : key_value_map) {
    argv.push_back("-s");
    std::string key_value_pair = key + "=" + value;
    argv.push_back(key_value_pair);
    log_msg += key_value_pair + " ";
  }

  static std::string unused_output;
  if (!base::GetAppOutput(argv, &unused_output)) {
    LOG(ERROR) << "Failed to flush " << log_msg << "into RO_PVD.";
    return false;
  }
  return true;
}

bool VpdUtilsImpl::GetRoVpd(const std::string& key, std::string* value) const {
  CHECK(value);
  if (auto it = cache_ro_.find(key); it != cache_ro_.end()) {
    *value = it->second;
    return true;
  }

  std::vector<std::string> argv{kVpdCmdPath, "-i", "RO_VPD", "-g", key};
  return base::GetAppOutput(argv, value);
}

bool VpdUtilsImpl::SetRwVpd(
    const std::map<std::string, std::string>& key_value_map) {
  std::string log_msg;
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RW_VPD"};
  for (const auto& [key, value] : key_value_map) {
    argv.push_back("-s");
    std::string key_value_pair = key + "=" + value;
    argv.push_back(key_value_pair);
    log_msg += key_value_pair + " ";
  }

  static std::string unused_output;
  if (!base::GetAppOutput(argv, &unused_output)) {
    LOG(ERROR) << "Failed to flush " << log_msg << "into RW_PVD.";
    return false;
  }
  return true;
}

bool VpdUtilsImpl::GetRwVpd(const std::string& key, std::string* value) const {
  CHECK(value);
  if (auto it = cache_rw_.find(key); it != cache_rw_.end()) {
    *value = it->second;
    return true;
  }

  std::vector<std::string> argv{kVpdCmdPath, "-i", "RW_VPD", "-g", key};
  return base::GetAppOutput(argv, value);
}

}  // namespace rmad
