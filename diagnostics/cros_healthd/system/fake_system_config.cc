// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_system_config.h"

#include <optional>
#include <utility>

namespace diagnostics {

FakeSystemConfig::FakeSystemConfig() = default;
FakeSystemConfig::~FakeSystemConfig() = default;

bool FakeSystemConfig::FioSupported() {
  return fio_supported_;
}

bool FakeSystemConfig::HasBacklight() {
  return has_backlight_;
}

bool FakeSystemConfig::HasBattery() {
  return has_battery_;
}

bool FakeSystemConfig::HasSmartBattery() {
  return has_smart_battery_;
}

bool FakeSystemConfig::HasSkuNumber() {
  return has_sku_number_property_;
}

bool FakeSystemConfig::NvmeSupported() {
  return nvme_supported_;
}

void FakeSystemConfig::NvmeSelfTestSupported(
    NvmeSelfTestSupportedCallback callback) {
  std::move(callback).Run(nvme_self_test_supported_);
}

bool FakeSystemConfig::SmartCtlSupported() {
  return smart_ctrl_supported_;
}

void FakeSystemConfig::SetFioSupported(bool value) {
  fio_supported_ = value;
}

bool FakeSystemConfig::IsWilcoDevice() {
  return wilco_device_;
}

std::optional<std::string> FakeSystemConfig::GetMarketingName() {
  return marketing_name_;
}

std::optional<std::string> FakeSystemConfig::GetOemName() {
  return oem_name_;
}

std::string FakeSystemConfig::GetCodeName() {
  return code_name_;
}

void FakeSystemConfig::SetHasBacklight(bool value) {
  has_backlight_ = value;
}

void FakeSystemConfig::SetHasBattery(bool value) {
  has_battery_ = value;
}

void FakeSystemConfig::SetHasSmartBattery(bool value) {
  has_smart_battery_ = value;
}

void FakeSystemConfig::SetHasSkuNumber(bool value) {
  has_sku_number_property_ = value;
}

void FakeSystemConfig::SetNvmeSupported(bool value) {
  nvme_supported_ = value;
}

void FakeSystemConfig::SetNvmeSelfTestSupported(bool value) {
  nvme_self_test_supported_ = value;
}

void FakeSystemConfig::SetSmartCtrlSupported(bool value) {
  smart_ctrl_supported_ = value;
}

void FakeSystemConfig::SetIsWilcoDevice(bool value) {
  wilco_device_ = value;
}

void FakeSystemConfig::SetMarketingName(
    const std::optional<std::string>& value) {
  marketing_name_ = value;
}

void FakeSystemConfig::SetOemName(const std::optional<std::string>& value) {
  oem_name_ = value;
}

void FakeSystemConfig::SetCodeName(const std::string& value) {
  code_name_ = value;
}

}  // namespace diagnostics
