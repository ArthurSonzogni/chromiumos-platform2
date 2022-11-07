// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_device_fake.h"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>

#include <chromeos/dbus/service_constants.h>

#include "lorgnette/constants.h"
#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"

namespace lorgnette {

SaneDeviceFake::SaneDeviceFake()
    : resolution_(100),
      source_name_("Fake source name"),
      color_mode_(MODE_COLOR),
      config_(ScannerConfig()),
      start_scan_result_(SANE_STATUS_GOOD),
      call_start_job_(true),
      read_scan_data_result_(SANE_STATUS_GOOD),
      cancel_scan_result_(true),
      scan_running_(false),
      cancelled_(false),
      max_read_size_(-1),
      initial_empty_reads_(0),
      num_empty_reads_(0) {}

SaneDeviceFake::~SaneDeviceFake() {}

std::optional<ValidOptionValues> SaneDeviceFake::GetValidOptionValues(
    brillo::ErrorPtr* error) {
  if (!values_.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No option values");
  }

  return values_;
}

bool SaneDeviceFake::SetScanResolution(brillo::ErrorPtr*, int resolution) {
  resolution_ = resolution;
  return true;
}

bool SaneDeviceFake::SetDocumentSource(brillo::ErrorPtr*,
                                       const std::string& source_name) {
  source_name_ = source_name;
  return true;
}

bool SaneDeviceFake::SetColorMode(brillo::ErrorPtr*, ColorMode color_mode) {
  color_mode_ = color_mode;
  return true;
}

bool SaneDeviceFake::SetScanRegion(brillo::ErrorPtr* error, const ScanRegion&) {
  return true;
}

SANE_Status SaneDeviceFake::StartScan(brillo::ErrorPtr* error) {
  // Don't allow starting the next page of the scan if we haven't completed the
  // previous one.
  if (scan_running_ && current_page_ < scan_data_.size() &&
      scan_data_offset_ < scan_data_[current_page_].size()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Scan is already running");
    return SANE_STATUS_DEVICE_BUSY;
  }

  if (cancelled_) {
    return SANE_STATUS_CANCELLED;
  }

  if (start_scan_result_ != SANE_STATUS_GOOD) {
    return start_scan_result_;
  }

  if (scan_running_ && current_page_ + 1 == scan_data_.size()) {
    // No more scan data left.
    return SANE_STATUS_NO_DOCS;
  } else if (scan_running_) {
    if (call_start_job_) {
      StartJob();
    }
    current_page_++;
    scan_data_offset_ = 0;
  } else {
    if (call_start_job_) {
      StartJob();
    }
    scan_running_ = true;
    current_page_ = 0;
    cancelled_ = false;
    scan_data_offset_ = 0;
  }

  return SANE_STATUS_GOOD;
}

SANE_Status SaneDeviceFake::GetScanParameters(brillo::ErrorPtr* error,
                                              ScanParameters* params) {
  if (!params_.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Parameters not set");
    return SANE_STATUS_INVAL;
  }

  *params = params_.value();
  return SANE_STATUS_GOOD;
}

SANE_Status SaneDeviceFake::ReadScanData(brillo::ErrorPtr* error,
                                         uint8_t* buf,
                                         size_t count,
                                         size_t* read_out) {
  if (!scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Scan not running");
    return SANE_STATUS_INVAL;
  }

  if (cancelled_) {
    scan_running_ = false;
    EndJob();
    return SANE_STATUS_CANCELLED;
  }

  if (read_scan_data_result_ != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Reading data failed");
    return read_scan_data_result_;
  }

  if (current_page_ >= scan_data_.size()) {
    scan_running_ = false;
    EndJob();
    return SANE_STATUS_NO_DOCS;
  }

  const std::vector<uint8_t>& page = scan_data_[current_page_];
  if (scan_data_offset_ >= page.size()) {
    *read_out = 0;
    return SANE_STATUS_EOF;
  }

  if (num_empty_reads_ < initial_empty_reads_) {
    ++num_empty_reads_;
    *read_out = 0;
    return SANE_STATUS_GOOD;
  }

  size_t to_copy = std::min(count, page.size() - scan_data_offset_);
  to_copy = std::min(to_copy, max_read_size_);
  memcpy(buf, page.data() + scan_data_offset_, to_copy);
  *read_out = to_copy;

  scan_data_offset_ += to_copy;
  return SANE_STATUS_GOOD;
}

bool SaneDeviceFake::CancelScan(brillo::ErrorPtr* error) {
  if (!scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Scan not running");
    return false;
  }

  cancelled_ = true;
  if (!cancel_scan_result_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Device cancel failed");
  }
  return cancel_scan_result_;
}

SANE_Status SaneDeviceFake::SetOption(brillo::ErrorPtr* error,
                                      const ScannerOption& option) {
  SANE_Status status;
  auto s = set_option_status_.find(option.name());
  if (s != set_option_status_.end()) {
    status = s->second;
  } else {
    status = SANE_STATUS_UNSUPPORTED;
  }
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Failed to set option");
  }
  return status;
}

void SaneDeviceFake::SetOptionStatus(const std::string& option,
                                     SANE_Status status) {
  set_option_status_[option] = status;
}

void SaneDeviceFake::SetCancelScanResult(bool result) {
  cancel_scan_result_ = result;
}

void SaneDeviceFake::ClearScanJob() {
  EndJob();
  cancelled_ = false;
  scan_running_ = false;
  current_page_ = 0;
  scan_data_offset_ = 0;
  num_empty_reads_ = 0;
}

void SaneDeviceFake::SetCallStartJob(bool call) {
  call_start_job_ = call;
}

std::optional<ScannerConfig> SaneDeviceFake::GetCurrentConfig(
    brillo::ErrorPtr* error) {
  if (!config_.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Failed to get config");
  }
  return config_;
}

void SaneDeviceFake::SetScannerConfig(
    const std::optional<ScannerConfig>& config) {
  config_ = config;
}

void SaneDeviceFake::SetValidOptionValues(
    const std::optional<ValidOptionValues>& values) {
  values_ = values;
}

void SaneDeviceFake::SetStartScanResult(SANE_Status status) {
  start_scan_result_ = status;
}

void SaneDeviceFake::SetScanParameters(
    const std::optional<ScanParameters>& params) {
  params_ = params;
}

void SaneDeviceFake::SetReadScanDataResult(SANE_Status result) {
  read_scan_data_result_ = result;
}

void SaneDeviceFake::SetScanData(
    const std::vector<std::vector<uint8_t>>& scan_data) {
  scan_data_ = scan_data;
}

void SaneDeviceFake::SetMaxReadSize(size_t read_size) {
  max_read_size_ = read_size;
}

void SaneDeviceFake::SetInitialEmptyReads(size_t num_empty) {
  initial_empty_reads_ = num_empty;
}

}  // namespace lorgnette
