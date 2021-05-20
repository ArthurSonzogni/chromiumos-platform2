/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_AE_INFO_H_
#define CAMERA_FEATURES_HDRNET_AE_INFO_H_

#include <cstdint>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/notreached.h>
#include <cros-camera/gcam_ae.h>
#include <cutils/native_handle.h>

#include "camera/camera_metadata.h"
#include "cros-camera/common_types.h"

namespace cros {

// Tags for metadata logger.
constexpr char kTagAeExposureCompensation[] = "ae_exposure_compensation";
constexpr char kTagAwbGains[] = "awb_rggb_gains";
constexpr char kTagCaptureAnalogGain[] = "analog_gain";
constexpr char kTagCaptureDigitalGain[] = "digital_gain";
constexpr char kTagCaptureExposureTimeNs[] = "exposure_time_ns";
constexpr char kTagCaptureSensitivity[] = "sensitivity";
constexpr char kTagCcm[] = "ccm";
constexpr char kTagEstimatedSensorSensitivity[] =
    "estimated_sensor_sensitivity";
constexpr char kTagFaceRectangles[] = "face_rectangles";
constexpr char kTagFilteredExpComp[] = "filtered_exposure_compensation";
constexpr char kTagFilteredLongTet[] = "filtered_long_tet";
constexpr char kTagFilteredShortTet[] = "filtered_short_tet";
constexpr char kTagFrameHeight[] = "frame_height";
constexpr char kTagFrameWidth[] = "frame_width";
constexpr char kTagHdrRatio[] = "hdr_ratio";
constexpr char kTagIpu6RgbsStatsBlocks[] = "ipu6.ae_stats.blocks";
constexpr char kTagIpu6RgbsStatsGridHeight[] = "ipu6.ae_stats.grid_height";
constexpr char kTagIpu6RgbsStatsGridWidth[] = "ipu6.ae_stats.grid_width";
constexpr char kTagIpu6RgbsStatsShadingCorrection[] =
    "ipu6.ae_stats.shading_correction";
constexpr char kTagLensAperture[] = "lens_aperture";
constexpr char kTagLongTet[] = "long_tet";
constexpr char kTagMaxHdrRatio[] = "max_hdr_ratio";
constexpr char kTagRequestAeCompensation[] = "request.ae_compensation";
constexpr char kTagRequestExpTime[] = "request.exposure_time_ns";
constexpr char kTagRequestSensitivity[] = "request.sensitivity";
constexpr char kTagShortTet[] = "short_tet";
constexpr char kTagWhiteLevel[] = "white_level";

// AeStatsInput is used to specify how Gcam AE computes the AE stats input to
// the AE algorithm.
enum class AeStatsInputMode {
  // Use vendor's AE stats to prepare AE algorithm input parameters.
  kFromVendorAeStats = 0,

  // Use YUV image to prepare AE algorithm input parameters.
  kFromYuvImage = 1,
};

enum class AeOverrideMode {
  // Let HdrNetAeController override AE decision with exposure compensation.
  kWithExposureCompensation = 0,

  // Let HdrNetAeController override AE decision with manual sensor control.
  kWithManualSensorControl = 1,
};

// A collection of all the info needed for producing the input arguments to the
// AE algorithm.
struct AeFrameInfo {
  int frame_number = -1;
  AeStatsInputMode ae_stats_input_mode = AeStatsInputMode::kFromVendorAeStats;
  bool use_cros_face_detector = true;
  Size active_array_dimension;

  float targeted_short_tet = 0.0f;
  float targeted_long_tet = 0.0f;
  float targeted_ae_compensation = 0.0f;

  // The settings used to capture the frame.
  float analog_gain = 0.0f;
  float digital_gain = 0.0f;
  float exposure_time_ms = 0.0f;
  int ae_compensation = 0;
  float estimated_sensor_sensitivity = 0.0f;
  uint8_t face_detection_mode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
  std::vector<NormalizedRect> faces;

  // The AWB gains and color correction matrix that will be applied to the
  // frame.
  float rggb_gains[4] = {0};
  float ccm[9] = {0};

  // The YUV buffer of the frame and the acquire fence of the YUV buffer.
  buffer_handle_t yuv_buffer = nullptr;
  base::ScopedFD acquire_fence;

  bool HasCaptureSettings() const {
    return exposure_time_ms > 0.0f && analog_gain > 0.0f &&
           digital_gain > 0.0f && estimated_sensor_sensitivity > 0.0f;
  }

  bool HasYuvBuffer() const { return yuv_buffer != nullptr; }

  bool IsValid() const {
    switch (ae_stats_input_mode) {
      case AeStatsInputMode::kFromVendorAeStats:
        if (use_cros_face_detector) {
          // Face detector needs YUV buffer.
          return HasCaptureSettings() && HasYuvBuffer();
        } else {
          return HasCaptureSettings();
        }
      case AeStatsInputMode::kFromYuvImage:
        return HasCaptureSettings() && HasYuvBuffer();
      default:
        NOTREACHED() << "Invalid AeStatsInputMode";
        return false;
    }
  }
};

struct AeParameters {
  // The Total Exposure Time (TET) that should be applied to the sensor for
  // capturing the image.
  float short_tet = 0.0f;

  // The ideal exposure time for HDR rendition.
  float long_tet = 0.0f;

  bool IsValid() { return short_tet > 0.0f && long_tet > 0.0f; }
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_AE_INFO_H_
