// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <span>
#include <vector>

#include "libec/fingerprint/fp_info_command.h"

namespace ec {

/**
 * @return An optional SensorId object. This will be empty if the command hasn't
 * been run or if no sensor id is available.
 */
std::optional<SensorId> FpInfoCommand_v3::sensor_id() {
  if (!Resp()) {
    return std::nullopt;
  }
  if (!sensor_id_.has_value()) {
    sensor_id_.emplace(
        SensorId{.vendor_id = Resp()->info.sensor_info.vendor_id,
                 .product_id = Resp()->info.sensor_info.product_id,
                 .model_id = Resp()->info.sensor_info.model_id,
                 .version = Resp()->info.sensor_info.version});
  }
  return sensor_id_;
}

/**
 * @return A SensorImage vector object. This will be empty if the command hasn't
 * been run or if no sensor image is available.
 */
std::vector<SensorImage> FpInfoCommand_v3::sensor_image() {
  if (!Resp()) {
    return {};
  }

  if (!sensor_image_.empty()) {
    return sensor_image_;
  }

  // FPMCU response is untrusted; clamp loop count to the protocol maximum,
  // the number of entries actually received, and the backing array size.
  const uint32_t actual_resp_size = ActualRespSize();
  if (actual_resp_size < kHeaderSize) {
    return {};
  }

  const uint32_t received_entries =
      (actual_resp_size - kHeaderSize) / kEntrySize;
  const auto frames = std::span(Resp()->image_frame_params);
  const uint32_t count = std::min<uint32_t>(
      {Resp()->info.sensor_info.num_capture_types,
       static_cast<uint32_t>(FP_MAX_CAPTURE_TYPES), received_entries,
       static_cast<uint32_t>(frames.size())});

  for (const auto& frame : frames.first(count)) {
    // b/569733038: Passing `frame.image_data_offset_bytes` directly to
    // std::optional's constructor binds a reference to a packed struct member,
    // which results in undefined behavior if `frame` isn't suitably aligned.
    // Copy into a local to guarantee correct alignment.
    const uint32_t image_data_offset_bytes = frame.image_data_offset_bytes;
    sensor_image_.emplace_back(SensorImage{
        .width = frame.width,
        .height = frame.height,
        .frame_size = frame.frame_size,
        .image_data_offset_bytes = image_data_offset_bytes,
        .pixel_format = frame.pixel_format,
        .bpp = frame.bpp,
        .fp_capture_type =
            static_cast<enum fp_capture_type>(frame.fp_capture_type),
    });
  }

  return sensor_image_;
}

/**
 * @return An optional TemplateInfo object. This will be empty if the command
 * hasn't been run or if no template info is available.
 */
std::optional<TemplateInfo> FpInfoCommand_v3::template_info() {
  if (!Resp()) {
    return std::nullopt;
  }
  if (!template_info_.has_value()) {
    template_info_.emplace(
        TemplateInfo{.version = Resp()->info.template_info.template_version,
                     .size = Resp()->info.template_info.template_size,
                     .max_templates = Resp()->info.template_info.template_max,
                     .num_valid = Resp()->info.template_info.template_valid,
                     .dirty = Resp()->info.template_info.template_dirty});
  }
  return template_info_;
}

/**
 * @return number of dead pixels or kDeadPixelsUnknown
 */
int FpInfoCommand_v3::NumDeadPixels() {
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
FpSensorErrors FpInfoCommand_v3::GetFpSensorErrors() {
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
