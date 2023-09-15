// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_DEVICE_IMPL_H_
#define LORGNETTE_SANE_DEVICE_IMPL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/synchronization/lock.h>
#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

#include "lorgnette/libsane_wrapper.h"
#include "lorgnette/sane_constraint.h"
#include "lorgnette/sane_device.h"
#include "lorgnette/sane_option.h"

namespace lorgnette {

using DeviceSet = std::pair<base::Lock, std::unordered_set<std::string>>;

class SaneDeviceImpl : public SaneDevice {
  friend class SaneClientImpl;

 public:
  ~SaneDeviceImpl();

  std::optional<ValidOptionValues> GetValidOptionValues(
      brillo::ErrorPtr* error) override;
  std::optional<ScannerConfig> GetCurrentConfig(
      brillo::ErrorPtr* error) override;

  std::optional<int> GetScanResolution(brillo::ErrorPtr* error) override;
  bool SetScanResolution(brillo::ErrorPtr* error, int resolution) override;
  std::optional<std::string> GetDocumentSource(
      brillo::ErrorPtr* error) override;
  bool SetDocumentSource(brillo::ErrorPtr* error,
                         const std::string& source_name) override;
  std::optional<ColorMode> GetColorMode(brillo::ErrorPtr* error) override;
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

 private:
  friend class SaneDeviceImplTest;
  friend class SaneDeviceImplPeer;

  enum ScanOption {
    kResolution,
    kScanMode,
    kSource,
    kJustificationX,
    kTopLeftX,
    kTopLeftY,
    kBottomRightX,
    kBottomRightY,
    kPageWidth,
    kPageHeight,
  };

  SaneDeviceImpl(LibsaneWrapper* libsane,
                 SANE_Handle handle,
                 const std::string& name,
                 std::shared_ptr<DeviceSet> open_devices);
  bool LoadOptions(brillo::ErrorPtr* error);
  bool UpdateDeviceOption(brillo::ErrorPtr* error, SaneOption* option);
  std::optional<ScannableArea> CalculateScannableArea(brillo::ErrorPtr* error);
  std::optional<double> GetOptionOffset(brillo::ErrorPtr* error,
                                        ScanOption option);

  const char* OptionDisplayName(ScanOption option);

  template <typename T>
  bool SetOption(brillo::ErrorPtr* error, ScanOption option, T value);
  template <typename T>
  std::optional<T> GetOption(brillo::ErrorPtr* error, ScanOption option);

  std::optional<std::vector<uint32_t>> GetResolutions(brillo::ErrorPtr* error);
  std::optional<std::vector<std::string>> GetColorModes(
      brillo::ErrorPtr* error);
  std::optional<uint32_t> GetJustificationXOffset(const ScanRegion& region,
                                                  brillo::ErrorPtr* error);
  std::optional<OptionRange> GetXRange(brillo::ErrorPtr* error);
  std::optional<OptionRange> GetYRange(brillo::ErrorPtr* error);

  LibsaneWrapper* libsane_;  // Not owned.
  SANE_Handle handle_;
  std::string name_;
  std::shared_ptr<DeviceSet> open_devices_;
  std::unordered_map<ScanOption, SaneOption> known_options_;
  std::unordered_map<std::string, SaneOption> all_options_;
  std::vector<lorgnette::OptionGroup> option_groups_;
  // This is true if we are currently acquiring an image frame (i.e. page) from
  // SANE. Once we've reached EOF for a frame, this will be false until
  // another call is made to StartScan().
  bool scan_running_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_DEVICE_IMPL_H_
