// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routine_parameter_fetcher.h"

#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/cros_healthd/routine_parameter_fetcher_constants.h"

namespace diagnostics {

RoutineParameterFetcher::RoutineParameterFetcher(
    brillo::CrosConfigInterface* cros_config)
    : cros_config_(cros_config) {
  CHECK(cros_config_);
}

RoutineParameterFetcher::~RoutineParameterFetcher() = default;

FingerprintParameter RoutineParameterFetcher::GetFingerprintParameters() const {
  FingerprintParameter param;
  FetchUint32Parameter(kFingerprintPropertiesPath, kMaxDeadPixels,
                       &param.max_dead_pixels);
  FetchUint32Parameter(kFingerprintPropertiesPath, kMaxDeadPixelsInDetectZone,
                       &param.max_dead_pixels_in_detect_zone);
  FetchUint32Parameter(kFingerprintPropertiesPath, kMaxPixelDev,
                       &param.max_pixel_dev);
  FetchUint32Parameter(kFingerprintPropertiesPath, kMaxErrorResetPixels,
                       &param.max_error_reset_pixels);
  FetchUint32Parameter(kFingerprintPropertiesPath, kMaxResetPixelDev,
                       &param.max_reset_pixel_dev);

  // Fill |FingerprintPixelMedian| value.
  FetchUint8Parameter(kFingerprintPixelMedianPath, kCbType1Lower,
                      &param.pixel_median.cb_type1_lower);
  FetchUint8Parameter(kFingerprintPixelMedianPath, kCbType1Upper,
                      &param.pixel_median.cb_type1_upper);
  FetchUint8Parameter(kFingerprintPixelMedianPath, kCbType2Lower,
                      &param.pixel_median.cb_type2_lower);
  FetchUint8Parameter(kFingerprintPixelMedianPath, kCbType2Upper,
                      &param.pixel_median.cb_type2_upper);
  FetchUint8Parameter(kFingerprintPixelMedianPath, kIcbType1Lower,
                      &param.pixel_median.icb_type1_lower);
  FetchUint8Parameter(kFingerprintPixelMedianPath, kIcbType1Upper,
                      &param.pixel_median.icb_type1_upper);
  FetchUint8Parameter(kFingerprintPixelMedianPath, kIcbType2Lower,
                      &param.pixel_median.icb_type2_lower);
  FetchUint8Parameter(kFingerprintPixelMedianPath, kIcbType2Upper,
                      &param.pixel_median.icb_type2_upper);

  // Fill |FingerprintZone| value;
  uint32_t num_detect_zone = 0;
  FetchUint32Parameter(kFingerprintPropertiesPath, kNumDetectZone,
                       &num_detect_zone);
  for (int i = 0; i < num_detect_zone; ++i) {
    base::FilePath path = base::FilePath(kFingerprintDetectZonesPath)
                              .Append(base::NumberToString(i));
    FingerprintZone zone;

    FetchUint32Parameter(path.value(), kX1, &zone.x1);
    FetchUint32Parameter(path.value(), kX2, &zone.x2);
    FetchUint32Parameter(path.value(), kY1, &zone.y1);
    FetchUint32Parameter(path.value(), kY2, &zone.y2);
    param.detect_zones.push_back(std::move(zone));
  }

  return param;
}

template <typename Uint64Type>
void RoutineParameterFetcher::FetchUint64Parameter(
    const std::string& path,
    const std::string& parameter_name,
    Uint64Type* parameter_out) const {
  DCHECK(parameter_out);

  std::string parameter_str;
  if (cros_config_->GetString(path, parameter_name, &parameter_str)) {
    uint64_t parameter;
    if (base::StringToUint64(parameter_str, &parameter)) {
      *parameter_out = parameter;
    } else {
      LOG(ERROR) << base::StringPrintf(
          "Failed to convert cros_config value: %s to uint64_t.",
          parameter_str.c_str());
    }
  }
}

template <typename Uint32Type>
void RoutineParameterFetcher::FetchUint32Parameter(
    const std::string& path,
    const std::string& parameter_name,
    Uint32Type* parameter_out) const {
  DCHECK(parameter_out);

  std::string parameter_str;
  if (cros_config_->GetString(path, parameter_name, &parameter_str)) {
    uint32_t parameter;
    if (base::StringToUint(parameter_str, &parameter)) {
      *parameter_out = parameter;
    } else {
      LOG(ERROR) << base::StringPrintf(
          "Failed to convert cros_config value: %s to uint32_t.",
          parameter_str.c_str());
    }
  }
}

template <typename Uint8Type>
void RoutineParameterFetcher::FetchUint8Parameter(
    const std::string& path,
    const std::string& parameter_name,
    Uint8Type* parameter_out) const {
  DCHECK(parameter_out);

  std::string parameter_str;
  if (cros_config_->GetString(path, parameter_name, &parameter_str)) {
    uint32_t parameter;
    if (base::StringToUint(parameter_str, &parameter)) {
      *parameter_out = static_cast<uint8_t>(parameter);
    } else {
      LOG(ERROR) << base::StringPrintf(
          "Failed to convert cros_config value: %s to uint8_t.",
          parameter_str.c_str());
    }
  }
}

}  // namespace diagnostics
