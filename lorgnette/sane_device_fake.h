// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_DEVICE_FAKE_H_
#define LORGNETTE_SANE_DEVICE_FAKE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sane/sane.h>

#include "lorgnette/sane_device.h"

namespace lorgnette {

class SaneDeviceFake : public SaneDevice {
 public:
  SaneDeviceFake();
  ~SaneDeviceFake();

  std::optional<ValidOptionValues> GetValidOptionValues(
      brillo::ErrorPtr* error) override;

  std::optional<int> GetScanResolution(brillo::ErrorPtr* error) override {
    return resolution_;
  }

  bool SetScanResolution(brillo::ErrorPtr* error, int resolution) override;
  std::optional<std::string> GetDocumentSource(
      brillo::ErrorPtr* error) override {
    return source_name_;
  }
  bool SetDocumentSource(brillo::ErrorPtr* error,
                         const std::string& source_name) override;
  std::optional<ColorMode> GetColorMode(brillo::ErrorPtr* error) override {
    return color_mode_;
  }
  bool SetColorMode(brillo::ErrorPtr* error, ColorMode color_mode) override;
  bool SetScanRegion(brillo::ErrorPtr* error,
                     const ScanRegion& region) override;
  SANE_Status StartScan(brillo::ErrorPtr* error) override;
  SANE_Status GetScanParameters(brillo::ErrorPtr* error,
                                ScanParameters* params) override;
  SANE_Status ReadScanData(brillo::ErrorPtr* error,
                           uint8_t* buf,
                           size_t count,
                           size_t* read_out) override;
  bool CancelScan(brillo::ErrorPtr* error) override;
  std::optional<ScannerConfig> GetCurrentConfig(
      brillo::ErrorPtr* error) override;

  void SetScannerConfig(const std::optional<ScannerConfig>& config);
  void SetValidOptionValues(const std::optional<ValidOptionValues>& values);
  void SetStartScanResult(SANE_Status status);
  void SetScanParameters(const std::optional<ScanParameters>& params);
  void SetReadScanDataResult(SANE_Status result);
  void SetScanData(const std::vector<std::vector<uint8_t>>& scan_data);
  void SetCancelScanResult(bool result);
  void ClearScanJob();
  void SetCallStartJob(bool call);

 private:
  int resolution_;
  std::string source_name_;
  ColorMode color_mode_;
  std::optional<ScannerConfig> config_;
  std::optional<ValidOptionValues> values_;
  SANE_Status start_scan_result_;
  bool call_start_job_;
  SANE_Status read_scan_data_result_;
  bool cancel_scan_result_;
  bool scan_running_;
  bool cancelled_;
  std::optional<ScanParameters> params_;
  std::vector<std::vector<uint8_t>> scan_data_;
  size_t current_page_;
  size_t scan_data_offset_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_DEVICE_FAKE_H_
