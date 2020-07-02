// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_fake.h"

#include <algorithm>
#include <map>
#include <utility>

#include <chromeos/dbus/service_constants.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"

namespace lorgnette {

// static
bool SaneClientFake::ListDevices(brillo::ErrorPtr* error,
                                 std::vector<ScannerInfo>* scanners_out) {
  if (!list_devices_result_)
    return false;

  *scanners_out = scanners_;
  return true;
}

std::unique_ptr<SaneDevice> SaneClientFake::ConnectToDevice(
    brillo::ErrorPtr* error, const std::string& device_name) {
  if (devices_.count(device_name) > 0) {
    auto ptr = std::move(devices_[device_name]);
    devices_.erase(device_name);
    return ptr;
  }

  brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                       kManagerServiceError, "No device");
  return nullptr;
}

void SaneClientFake::SetListDevicesResult(bool value) {
  list_devices_result_ = value;
}

void SaneClientFake::AddDevice(const std::string& name,
                               const std::string& manufacturer,
                               const std::string& model,
                               const std::string& type) {
  ScannerInfo info;
  info.set_name(name);
  info.set_manufacturer(manufacturer);
  info.set_model(model);
  info.set_type(type);
  scanners_.push_back(info);
}

void SaneClientFake::RemoveDevice(const std::string& name) {
  for (auto it = scanners_.begin(); it != scanners_.end(); it++) {
    if (it->name() == name) {
      scanners_.erase(it);
    }
  }
}

void SaneClientFake::SetDeviceForName(const std::string& device_name,
                                      std::unique_ptr<SaneDeviceFake> device) {
  devices_.emplace(device_name, std::move(device));
}

SaneDeviceFake::SaneDeviceFake()
    : start_scan_result_(true),
      read_scan_data_result_(true),
      scan_running_(false) {}

SaneDeviceFake::~SaneDeviceFake() {}

bool SaneDeviceFake::GetValidOptionValues(brillo::ErrorPtr* error,
                                          ValidOptionValues* values) {
  if (!values || !values_.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No option values");
    return false;
  }

  *values = values_.value();
  return true;
}

bool SaneDeviceFake::SetScanResolution(brillo::ErrorPtr*, int) {
  return true;
}

bool SaneDeviceFake::SetDocumentSource(brillo::ErrorPtr*,
                                       const DocumentSource&) {
  return true;
}

bool SaneDeviceFake::SetColorMode(brillo::ErrorPtr*, ColorMode) {
  return true;
}

bool SaneDeviceFake::StartScan(brillo::ErrorPtr* error) {
  if (scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Scan not running");
    return false;
  }

  if (!start_scan_result_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Starting failed.");
    return false;
  }

  scan_running_ = true;
  scan_data_offset_ = 0;
  return true;
}

bool SaneDeviceFake::GetScanParameters(brillo::ErrorPtr* error,
                                       ScanParameters* parameters) {
  if (!parameters || !params_.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No parameters");
    return false;
  }

  *parameters = params_.value();
  return true;
}

bool SaneDeviceFake::ReadScanData(brillo::ErrorPtr* error,
                                  uint8_t* buf,
                                  size_t count,
                                  size_t* read_out) {
  if (!scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Scan not running");
    return false;
  }

  if (!read_scan_data_result_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Reading data failed");
    return false;
  }

  if (scan_data_offset_ >= scan_data_.size()) {
    scan_running_ = false;
    *read_out = 0;
    return true;
  }

  size_t to_copy = std::min(count, scan_data_.size() - scan_data_offset_);
  memcpy(buf, scan_data_.data() + scan_data_offset_, to_copy);
  *read_out = to_copy;

  scan_data_offset_ += to_copy;
  return true;
}

void SaneDeviceFake::SetValidOptionValues(
    const base::Optional<ValidOptionValues>& values) {
  values_ = values;
}

void SaneDeviceFake::SetStartScanResult(bool result) {
  start_scan_result_ = result;
}

void SaneDeviceFake::SetScanParameters(
    const base::Optional<ScanParameters>& params) {
  params_ = params;
}

void SaneDeviceFake::SetReadScanDataResult(bool result) {
  read_scan_data_result_ = result;
}

void SaneDeviceFake::SetScanData(const std::vector<uint8_t>& scan_data) {
  scan_data_ = scan_data;
}

}  // namespace lorgnette
