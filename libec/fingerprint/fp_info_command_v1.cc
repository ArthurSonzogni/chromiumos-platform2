// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_info_command.h"

namespace ec {

/**
 * @return An optional SensorId object. This will be empty if the command hasn't
 * been run or if no sensor id is available.
 */
std::optional<SensorId> FpInfoCommand_v1::sensor_id() {
  if (!Resp()) {
    return std::nullopt;
  }
  if (!sensor_id_.has_value()) {
    sensor_id_.emplace(Resp()->vendor_id, Resp()->product_id, Resp()->model_id,
                       Resp()->version);
  }
  return sensor_id_;
}

/**
 * @return An optional SensorImage object. This will be empty if the command
 * hasn't been run or if no sensor image is available.
 */
std::optional<SensorImage> FpInfoCommand_v1::sensor_image() {
  if (!Resp()) {
    return std::nullopt;
  }
  if (!sensor_image_.has_value()) {
    sensor_image_.emplace(Resp()->width, Resp()->height, Resp()->frame_size,
                          Resp()->pixel_format, Resp()->bpp);
  }
  return sensor_image_;
}

/**
 * @return An optional TemplateInfo object. This will be empty if the command
 * hasn't been run or if no template info is available.
 */
std::optional<TemplateInfo> FpInfoCommand_v1::template_info() {
  if (!Resp()) {
    return std::nullopt;
  }
  if (!template_info_.has_value()) {
    template_info_.emplace(Resp()->template_version, Resp()->template_size,
                           Resp()->template_max, Resp()->template_valid,
                           Resp()->template_dirty);
  }
  return template_info_;
}

/**
 * @return number of dead pixels or kDeadPixelsUnknown
 */
int FpInfoCommand_v1::NumDeadPixels() {
  if (!Resp()) {
    return FpInfoCommand::kDeadPixelsUnknown;
  }
  uint16_t num_dead_pixels = FP_ERROR_DEAD_PIXELS(Resp()->errors);
  if (num_dead_pixels == FP_ERROR_DEAD_PIXELS_UNKNOWN) {
    return FpInfoCommand::kDeadPixelsUnknown;
  }
  return num_dead_pixels;
}

/**
 * @return FpSensorErrors
 */
FpSensorErrors FpInfoCommand_v1::GetFpSensorErrors() {
  FpSensorErrors ret = FpSensorErrors::kNone;

  if (!Resp()) {
    return ret;
  }

  auto errors = Resp()->errors;

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
