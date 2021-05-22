// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_IMPL_H_
#define LORGNETTE_SANE_CLIENT_IMPL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/optional.h>
#include <base/synchronization/lock.h>
#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

#include "lorgnette/sane_client.h"

namespace lorgnette {

using DeviceSet = std::pair<base::Lock, std::unordered_set<std::string>>;

class SaneClientImpl : public SaneClient {
 public:
  static std::unique_ptr<SaneClientImpl> Create();
  ~SaneClientImpl();

  base::Optional<std::vector<ScannerInfo>> ListDevices(
      brillo::ErrorPtr* error) override;

  static base::Optional<std::vector<ScannerInfo>> DeviceListToScannerInfo(
      const SANE_Device** device_list);

 protected:
  std::unique_ptr<SaneDevice> ConnectToDeviceInternal(
      brillo::ErrorPtr* error,
      SANE_Status* sane_status,
      const std::string& device_name) override;

 private:
  SaneClientImpl();

  base::Lock lock_;
  std::shared_ptr<DeviceSet> open_devices_;
};

class SaneOption {
 public:
  SaneOption(const SANE_Option_Descriptor& opt, int index);

  bool Set(double d);
  bool Set(int i);
  bool Set(const std::string& s);

  template <typename T>
  base::Optional<T> Get() const = delete;
  template <>
  base::Optional<int> Get() const;
  template <>
  base::Optional<std::string> Get() const;

  // This returns a pointer to the internal storage. Care must be taken that the
  // pointer does not outlive the SaneOption.
  void* GetPointer();

  int GetIndex() const;
  std::string GetName() const;
  std::string DisplayValue() const;

 private:
  std::string name_;
  int index_;
  SANE_Value_Type type_;  // The type that the backend uses for the option.

  // The integer data, if this is an int option.
  union {
    SANE_Int i;
    SANE_Fixed f;
  } int_data_;

  // The buffer containing string data, if this is a string option.
  std::vector<char> string_data_;
};

// Represents the possible values for an option.
struct OptionRange {
  double start;
  double size;
};

class SaneDeviceImpl : public SaneDevice {
  friend class SaneClientImpl;

 public:
  ~SaneDeviceImpl();

  base::Optional<ValidOptionValues> GetValidOptionValues(
      brillo::ErrorPtr* error) override;

  base::Optional<int> GetScanResolution(brillo::ErrorPtr* error) override;
  bool SetScanResolution(brillo::ErrorPtr* error, int resolution) override;
  base::Optional<std::string> GetDocumentSource(
      brillo::ErrorPtr* error) override;
  bool SetDocumentSource(brillo::ErrorPtr* error,
                         const std::string& source_name) override;
  bool SetColorMode(brillo::ErrorPtr* error, ColorMode color_mode) override;
  bool SetScanRegion(brillo::ErrorPtr* error,
                     const ScanRegion& region) override;
  SANE_Status StartScan(brillo::ErrorPtr* error) override;
  base::Optional<ScanParameters> GetScanParameters(
      brillo::ErrorPtr* error) override;
  SANE_Status ReadScanData(brillo::ErrorPtr* error,
                           uint8_t* buf,
                           size_t count,
                           size_t* read_out) override;
  bool CancelScan(brillo::ErrorPtr* error) override;

  static base::Optional<std::vector<std::string>> GetValidStringOptionValues(
      brillo::ErrorPtr* error, const SANE_Option_Descriptor& opt);

  static base::Optional<std::vector<uint32_t>> GetValidIntOptionValues(
      brillo::ErrorPtr* error, const SANE_Option_Descriptor& opt);

  static base::Optional<OptionRange> GetOptionRange(
      brillo::ErrorPtr* error, const SANE_Option_Descriptor& opt);

 private:
  friend class SaneDeviceImplTest;

  enum ScanOption {
    kResolution,
    kScanMode,
    kSource,
    kJustificationX,
    kTopLeftX,
    kTopLeftY,
    kBottomRightX,
    kBottomRightY,
  };

  SaneDeviceImpl(SANE_Handle handle,
                 const std::string& name,
                 std::shared_ptr<DeviceSet> open_devices);
  bool LoadOptions(brillo::ErrorPtr* error);
  bool UpdateDeviceOption(brillo::ErrorPtr* error, SaneOption* option);
  base::Optional<ScannableArea> CalculateScannableArea(brillo::ErrorPtr* error);
  base::Optional<double> GetOptionOffset(brillo::ErrorPtr* error,
                                         ScanOption option);

  const char* OptionDisplayName(ScanOption option);

  template <typename T>
  bool SetOption(brillo::ErrorPtr* error, ScanOption option, T value);
  template <typename T>
  base::Optional<T> GetOption(brillo::ErrorPtr* error, ScanOption option);

  base::Optional<std::vector<uint32_t>> GetResolutions(brillo::ErrorPtr* error);
  base::Optional<std::vector<std::string>> GetColorModes(
      brillo::ErrorPtr* error);
  base::Optional<uint32_t> GetJustificationXOffset(const ScanRegion& region,
                                                   brillo::ErrorPtr* error);
  base::Optional<OptionRange> GetXRange(brillo::ErrorPtr* error);

  SANE_Handle handle_;
  std::string name_;
  std::shared_ptr<DeviceSet> open_devices_;
  std::unordered_map<ScanOption, SaneOption> options_;
  // This is true if we are currently acquiring an image frame (i.e. page) from
  // SANE. Once we've reached EOF for a frame, this will be false until
  // another call is made to StartScan().
  bool scan_running_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_IMPL_H_
