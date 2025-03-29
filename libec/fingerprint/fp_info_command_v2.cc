// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "libec/fingerprint/fp_info_command.h"

namespace ec {

/**
 * @return An optional SensorId object. This will be empty if the command hasn't
 * been run or if no sensor id is available.
 */
std::optional<SensorId> FpInfoCommand_v2::sensor_id() {
  if (!Resp()) {
    return std::nullopt;
  }
  if (!sensor_id_.has_value()) {
    sensor_id_.emplace(
        Resp()->info.sensor_info.vendor_id, Resp()->info.sensor_info.product_id,
        Resp()->info.sensor_info.model_id, Resp()->info.sensor_info.version);
  }
  return sensor_id_;
}

/**
 * @return A SensorImage vector object. This will be empty if the command hasn't
 * been run or if no sensor image is available.
 */
std::vector<SensorImage> FpInfoCommand_v2::sensor_image() {
  if (!Resp()) {
    return {};
  }

  if (!sensor_image_.empty()) {
    return sensor_image_;
  }

  uint32_t count = Resp()->info.sensor_info.num_capture_types;

  for (uint32_t i = 0; i < count; ++i) {
    sensor_image_.emplace_back(Resp()->image_frame_params[i].width,
                               Resp()->image_frame_params[i].height,
                               Resp()->image_frame_params[i].frame_size,
                               Resp()->image_frame_params[i].pixel_format,
                               Resp()->image_frame_params[i].bpp);
  }

  return sensor_image_;
}

/**
 * @return An optional TemplateInfo object. This will be empty if the command
 * hasn't been run or if no template info is available.
 */
std::optional<TemplateInfo> FpInfoCommand_v2::template_info() {
  if (!Resp()) {
    return std::nullopt;
  }
  if (!template_info_.has_value()) {
    template_info_.emplace(Resp()->info.template_info.template_version,
                           Resp()->info.template_info.template_size,
                           Resp()->info.template_info.template_max,
                           Resp()->info.template_info.template_valid,
                           Resp()->info.template_info.template_dirty);
  }
  return template_info_;
}

/**
 * @return number of dead pixels or kDeadPixelsUnknown
 */
int FpInfoCommand_v2::NumDeadPixels() {
  if (!Resp()) {
    return FpInfoCommand::kDeadPixelsUnknown;
  }
  uint16_t num_dead_pixels =
      FP_ERROR_DEAD_PIXELS(Resp()->info.sensor_info.errors);
  if (num_dead_pixels == FP_ERROR_DEAD_PIXELS_UNKNOWN) {
    return FpInfoCommand::kDeadPixelsUnknown;
  }
  return num_dead_pixels;
}

/**
 * @return FpSensorErrors
 */
FpSensorErrors FpInfoCommand_v2::GetFpSensorErrors() {
  FpSensorErrors ret = FpSensorErrors::kNone;

  if (!Resp()) {
    return ret;
  }

  auto errors = Resp()->info.sensor_info.errors;

  if (errors & FP_ERROR_NO_IRQ) {
    ret |= FpSensorErrors::kNoIrq;
  }
  if (errors & FP_ERROR_BAD_HWID) {
    ret |= FpSensorErrors::kBadHardwareID;
  }
  if (errors & FP_ERROR_INIT_FAIL) {
    ret |= FpSensorErrors::kInitializationFailure;
  }
  if (errors & FP_ERROR_SPI_COMM) {
    ret |= FpSensorErrors::kSpiCommunication;
  }
  if ((FP_ERROR_DEAD_PIXELS(errors) != FP_ERROR_DEAD_PIXELS_UNKNOWN) &&
      (FP_ERROR_DEAD_PIXELS(errors) != 0)) {
    ret |= FpSensorErrors::kDeadPixels;
  }

  return ret;
}

}  // namespace ec
