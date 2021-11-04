// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_vpd_utils.h"

#include <map>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/constants.h"
#include "rmad/utils/json_store.h"

namespace {

constexpr char kVpdKeySerialNumber[] = "serial_number";
constexpr char kVpdKeyWhitelabelTag[] = "whitelabel_tag";
constexpr char kVpdKeyRegion[] = "region";
constexpr char kVpdKeyUbindAttribute[] = "ubind_attribute";
constexpr char kVpdKeyGbindAttribute[] = "gbind_attribute";
constexpr char kVpdKeyStableDeviceSecret[] = "stable_device_secret";

}  // namespace

namespace rmad {
namespace fake {

FakeVpdUtils::FakeVpdUtils(const base::FilePath& working_dir_path)
    : VpdUtils(), working_dir_path_(working_dir_path) {
  json_store_ = base::MakeRefCounted<JsonStore>(
      working_dir_path_.AppendASCII(kVpdFilePath));
  CHECK(!json_store_->ReadOnly());
}

bool FakeVpdUtils::GetSerialNumber(std::string* serial_number) const {
  CHECK(serial_number);

  return json_store_->GetValue(kVpdKeySerialNumber, serial_number);
}

bool FakeVpdUtils::GetWhitelabelTag(std::string* whitelabel_tag) const {
  CHECK(whitelabel_tag);

  // We can allow whitelabel-tag to be empty.
  if (!json_store_->GetValue(kVpdKeyWhitelabelTag, whitelabel_tag)) {
    *whitelabel_tag = "";
  }

  return true;
}

bool FakeVpdUtils::GetRegion(std::string* region) const {
  CHECK(region);

  return json_store_->GetValue(kVpdKeyRegion, region);
}

bool FakeVpdUtils::GetCalibbias(const std::vector<std::string>& entries,
                                std::vector<int>* calibbias) const {
  CHECK(calibbias);

  std::vector<int> values;
  for (const std::string& entry : entries) {
    int val;
    if (!json_store_->GetValue(entry, &val)) {
      return false;
    }
    values.push_back(val);
  }

  *calibbias = values;
  return true;
}

bool FakeVpdUtils::GetRegistrationCode(std::string* ubind,
                                       std::string* gbind) const {
  CHECK(ubind);
  CHECK(gbind);

  return json_store_->GetValue(kVpdKeyUbindAttribute, ubind) &&
         json_store_->GetValue(kVpdKeyGbindAttribute, gbind);
}

bool FakeVpdUtils::GetStableDeviceSecret(
    std::string* stable_device_secret) const {
  CHECK(stable_device_secret);

  return json_store_->GetValue(kVpdKeyStableDeviceSecret, stable_device_secret);
}

bool FakeVpdUtils::SetSerialNumber(const std::string& serial_number) {
  return json_store_->SetValue(kVpdKeySerialNumber, serial_number);
}

bool FakeVpdUtils::SetWhitelabelTag(const std::string& whitelabel_tag) {
  return json_store_->SetValue(kVpdKeyWhitelabelTag, whitelabel_tag);
}

bool FakeVpdUtils::SetRegion(const std::string& region) {
  return json_store_->SetValue(kVpdKeyRegion, region);
}

bool FakeVpdUtils::SetCalibbias(const std::map<std::string, int>& calibbias) {
  bool ret = true;
  for (const auto& [key, value] : calibbias) {
    ret &= json_store_->SetValue(key, value);
  }
  return ret;
}

bool FakeVpdUtils::SetRegistrationCode(const std::string& ubind,
                                       const std::string& gbind) {
  return json_store_->SetValue(kVpdKeyUbindAttribute, ubind) &&
         json_store_->SetValue(kVpdKeyGbindAttribute, gbind);
}

bool FakeVpdUtils::SetStableDeviceSecret(
    const std::string& stable_device_secret) {
  return json_store_->SetValue(kVpdKeyStableDeviceSecret, stable_device_secret);
}

bool FakeVpdUtils::FlushOutRoVpdCache() {
  return true;
}

bool FakeVpdUtils::FlushOutRwVpdCache() {
  return true;
}

}  // namespace fake
}  // namespace rmad
