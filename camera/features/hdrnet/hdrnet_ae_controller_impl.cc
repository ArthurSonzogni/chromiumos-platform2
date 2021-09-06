/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_ae_controller_impl.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"

namespace cros {

namespace {

// IIR filter on log2 space:
//   exp2(|strength| * log2(current_value) + (1 - |strength|) * log2(new_value))
float IirFilterLog2(float current_value, float new_value, float strength) {
  constexpr float kTetEpsilon = 1.0e-6f;
  current_value = std::max(current_value, kTetEpsilon);
  new_value = std::max(new_value, kTetEpsilon);
  const float curr_log = std::log2f(current_value);
  const float new_log = std::log2f(new_value);
  const float next_log = strength * curr_log + (1 - strength) * new_log;
  return std::exp2f(next_log);
}

float LookUpHdrRatio(const base::flat_map<float, float>& max_hdr_ratio,
                     float gain) {
  DCHECK(!max_hdr_ratio.empty());
  for (auto it = max_hdr_ratio.rbegin(); it != max_hdr_ratio.rend(); it++) {
    if (it->first <= gain) {
      auto prev = (it == max_hdr_ratio.rbegin()) ? it : it - 1;
      const float min_gain = it->first;
      const float min_ratio = it->second;
      const float max_gain = prev->first;
      const float max_ratio = prev->second;
      const float slope = (max_ratio - min_ratio) / (max_gain - min_gain);
      return min_ratio + slope * (gain - min_gain);
    }
  }
  // Default to the HDR ratio at the maximum gain, which is usually the smallest
  // one.
  return max_hdr_ratio.rbegin()->second;
}

}  // namespace

// static
std::unique_ptr<HdrNetAeController> HdrNetAeControllerImpl::CreateInstance(
    const camera_metadata_t* static_info) {
  return std::make_unique<HdrNetAeControllerImpl>(
      static_info, HdrNetAeDeviceAdapter::CreateInstance());
}

HdrNetAeControllerImpl::HdrNetAeControllerImpl(
    const camera_metadata_t* static_info,
    std::unique_ptr<HdrNetAeDeviceAdapter> ae_device_adapter)
    : face_detector_(FaceDetector::Create()),
      ae_device_adapter_(std::move(ae_device_adapter)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);

  base::span<const int32_t> sensitivity_range = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_SENSITIVITY_RANGE);
  base::Optional<int32_t> max_analog_sensitivity = GetRoMetadata<int32_t>(
      static_info, ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY);
  base::Optional<Rational> ae_compensation_step = GetRoMetadata<Rational>(
      static_info, ANDROID_CONTROL_AE_COMPENSATION_STEP);
  base::span<const int32_t> ae_compensation_range =
      GetRoMetadataAsSpan<int32_t>(static_info,
                                   ANDROID_CONTROL_AE_COMPENSATION_RANGE);
  base::span<const int32_t> active_array_size = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);

  DCHECK_EQ(sensitivity_range.size(), 2);
  DCHECK_NE(sensitivity_range[0], 0);
  DCHECK(max_analog_sensitivity);
  DCHECK(ae_compensation_step);
  DCHECK_NE(ae_compensation_step->denominator, 0);
  DCHECK_EQ(ae_compensation_range.size(), 2);
  DCHECK_EQ(active_array_size.size(), 4);

  VLOGF(2) << "sensitivity_range: " << sensitivity_range[0] << " - "
           << sensitivity_range[1];
  VLOGF(2) << "max_analog_sensitivity: " << *max_analog_sensitivity;
  VLOGF(2) << "ae_compensation_step: " << ae_compensation_step->numerator << "/"
           << ae_compensation_step->denominator;
  VLOGF(2) << "ae_compensation_range: " << ae_compensation_range[0] << " - "
           << ae_compensation_range[1];
  VLOGF(2) << "active_array_size: (" << active_array_size[0] << ", "
           << active_array_size[1] << "), (" << active_array_size[2] << ", "
           << active_array_size[3] << ")";

  sensitivity_range_ = Range<int>(sensitivity_range[0], sensitivity_range[1]);
  max_analog_gain_ =
      static_cast<float>(*max_analog_sensitivity) / sensitivity_range[0];
  ae_compensation_step_ = (static_cast<float>(ae_compensation_step->numerator) /
                           ae_compensation_step->denominator);
  ae_compensation_range_ =
      Range<int>(ae_compensation_range[0], ae_compensation_range[1]);
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);
}

void HdrNetAeControllerImpl::RecordYuvBuffer(int frame_number,
                                             buffer_handle_t buffer,
                                             base::ScopedFD acquire_fence) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AeFrameInfo& frame_info = GetOrCreateAeFrameInfoEntry(frame_number);

  // TODO(jcliang): Face detection doesn't work too well on the under-exposed
  // frames in dark scenes. We should perhaps run face detection on the
  // HDRnet-rendered frames.
  if (use_cros_face_detector_) {
    if (ShouldRunFd(frame_number)) {
      std::vector<human_sensing::CrosFace> facessd_faces;
      auto ret = face_detector_->Detect(buffer, &facessd_faces,
                                        active_array_dimension_);
      std::vector<NormalizedRect> faces;
      if (ret != FaceDetectResult::kDetectOk) {
        LOGF(WARNING) << "Cannot run face detection";
      } else {
        for (auto& f : facessd_faces) {
          faces.push_back(NormalizedRect{
              .x0 =
                  std::clamp(f.bounding_box.x1 / active_array_dimension_.width,
                             0.0f, 1.0f),
              .x1 =
                  std::clamp(f.bounding_box.x2 / active_array_dimension_.width,
                             0.0f, 1.0f),
              .y0 =
                  std::clamp(f.bounding_box.y1 / active_array_dimension_.height,
                             0.0f, 1.0f),
              .y1 =
                  std::clamp(f.bounding_box.y2 / active_array_dimension_.height,
                             0.0f, 1.0f)});
        }
      }
      latest_faces_ = std::move(faces);
    }
    frame_info.faces =
        base::make_optional<std::vector<NormalizedRect>>(latest_faces_);
  }

  if ((ae_stats_input_mode_ == AeStatsInputMode::kFromYuvImage) &&
      ShouldRunAe(frame_number)) {
    frame_info.yuv_buffer = buffer;
    frame_info.acquire_fence = std::move(acquire_fence);
  }

  MaybeRunAE(frame_number);
}

void HdrNetAeControllerImpl::RecordAeMetadata(
    Camera3CaptureDescriptor* result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AeFrameInfo& frame_info = GetOrCreateAeFrameInfoEntry(result->frame_number());

  // Exposure and gain info.
  base::span<const int32_t> sensitivity =
      result->GetMetadata<int32_t>(ANDROID_SENSOR_SENSITIVITY);
  if (sensitivity.empty()) {
    LOGF(WARNING) << "Cannot get ANDROID_SENSOR_SENSITIVITY";
    return;
  }
  base::span<const int64_t> exposure_time_ns =
      result->GetMetadata<int64_t>(ANDROID_SENSOR_EXPOSURE_TIME);
  if (exposure_time_ns.empty()) {
    LOGF(WARNING) << "Cannot get ANDROID_SENSOR_EXPOSURE_TIME";
    return;
  }
  base::span<const float> aperture =
      result->GetMetadata<float>(ANDROID_LENS_APERTURE);
  if (aperture.empty()) {
    LOGF(WARNING) << "Cannot get ANDROID_LENS_APERTURE";
    return;
  }
  base::span<const int32_t> ae_compensation =
      result->GetMetadata<int32_t>(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION);
  if (ae_compensation.empty()) {
    LOGF(WARNING) << "Cannot get ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION";
    return;
  }
  if (ae_compensation[0] < ae_compensation_range_.lower() ||
      ae_compensation[0] > ae_compensation_range_.upper()) {
    LOGFID(WARNING, result->frame_number())
        << "Invalid AE compensation value: " << ae_compensation[0];
    return;
  }
  base::span<const uint8_t> face_detect_mode =
      result->GetMetadata<uint8_t>(ANDROID_STATISTICS_FACE_DETECT_MODE);
  if (face_detect_mode.empty()) {
    LOGF(WARNING) << "Cannot get ANDROID_STATISTICS_FACE_DETECT_MODE";
    return;
  }

  float total_gain =
      base::checked_cast<float>(sensitivity[0]) / sensitivity_range_.lower();
  float analog_gain = std::min(total_gain, max_analog_gain_);
  float digital_gain = std::max(total_gain / max_analog_gain_, 1.0f);

  frame_info.exposure_time_ms =
      base::checked_cast<float>(exposure_time_ns[0]) / 1'000'000;
  frame_info.analog_gain = analog_gain;
  frame_info.digital_gain = digital_gain;
  frame_info.estimated_sensor_sensitivity =
      (base::checked_cast<float>(sensitivity_range_.lower()) /
       (aperture[0] * aperture[0]));
  frame_info.ae_compensation = ae_compensation[0];
  frame_info.face_detection_mode = face_detect_mode[0];

  if (metadata_logger_) {
    metadata_logger_->Log(result->frame_number(), kTagCaptureExposureTimeNs,
                          exposure_time_ns[0]);
    metadata_logger_->Log(result->frame_number(), kTagCaptureSensitivity,
                          sensitivity[0]);
    metadata_logger_->Log(result->frame_number(), kTagCaptureAnalogGain,
                          analog_gain);
    metadata_logger_->Log(result->frame_number(), kTagCaptureDigitalGain,
                          digital_gain);
    metadata_logger_->Log(result->frame_number(),
                          kTagEstimatedSensorSensitivity,
                          frame_info.estimated_sensor_sensitivity);
    metadata_logger_->Log(result->frame_number(), kTagLensAperture,
                          aperture[0]);
    metadata_logger_->Log(result->frame_number(), kTagAeExposureCompensation,
                          ae_compensation[0]);
  }

  // Face info.
  if (!use_cros_face_detector_) {
    base::span<const int32_t> face_rectangles =
        result->GetMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES);
    std::vector<NormalizedRect> faces;
    if (face_rectangles.size() >= 4) {
      for (size_t i = 0; i < face_rectangles.size(); i += 4) {
        const int* rect_bound = &face_rectangles[i];
        faces.push_back(NormalizedRect{
            .x0 = std::clamp(base::checked_cast<float>(rect_bound[0]) /
                                 active_array_dimension_.width,
                             0.0f, 1.0f),
            .x1 = std::clamp(base::checked_cast<float>(rect_bound[2]) /
                                 active_array_dimension_.width,
                             0.0f, 1.0f),
            .y0 = std::clamp(base::checked_cast<float>(rect_bound[1]) /
                                 active_array_dimension_.height,
                             0.0f, 1.0f),
            .y1 = std::clamp(base::checked_cast<float>(rect_bound[3]) /
                                 active_array_dimension_.height,
                             0.0f, 1.0f)});
      }
    }
    frame_info.faces =
        base::make_optional<std::vector<NormalizedRect>>(std::move(faces));
    if (metadata_logger_) {
      metadata_logger_->Log(result->frame_number(), kTagFaceRectangles,
                            face_rectangles);
    }
  }

  // AWB info.
  base::span<const float> color_correction_gains =
      result->GetMetadata<float>(ANDROID_COLOR_CORRECTION_GAINS);
  if (!color_correction_gains.empty()) {
    CHECK_EQ(color_correction_gains.size(), 4);
    memcpy(frame_info.rggb_gains, color_correction_gains.data(),
           4 * sizeof(float));
    VLOGFID(2, result->frame_number())
        << "AWB gains: " << frame_info.rggb_gains[0] << ", "
        << frame_info.rggb_gains[1] << ", " << frame_info.rggb_gains[2] << ", "
        << frame_info.rggb_gains[3];
  } else {
    LOGF(WARNING) << "Cannot get ANDROID_COLOR_CORRECTION_GAINS";
  }

  if (metadata_logger_) {
    metadata_logger_->Log(result->frame_number(), kTagAwbGains,
                          color_correction_gains);
  }

  // CCM
  base::span<const camera_metadata_rational_t> color_correction_transform =
      result->GetMetadata<camera_metadata_rational_t>(
          ANDROID_COLOR_CORRECTION_TRANSFORM);
  if (!color_correction_transform.empty()) {
    CHECK_EQ(color_correction_transform.size(), 9);
    for (int i = 0; i < 9; ++i) {
      frame_info.ccm[i] =
          static_cast<float>(color_correction_transform[i].numerator) /
          color_correction_transform[i].denominator;
    }
    VLOGFID(2, result->frame_number())
        << "CCM: " << frame_info.ccm[0] << ", " << frame_info.ccm[1] << ", "
        << frame_info.ccm[2] << ", " << frame_info.ccm[3] << ", "
        << frame_info.ccm[4] << ", " << frame_info.ccm[5] << ", "
        << frame_info.ccm[6] << ", " << frame_info.ccm[7] << ", "
        << frame_info.ccm[8];

  } else {
    LOGF(WARNING) << "Cannot get ANDROID_COLOR_CORRECTION_TRANSFORM";
  }

  if (metadata_logger_) {
    metadata_logger_->Log(result->frame_number(), kTagCcm,
                          color_correction_transform);
  }

  // AE stats.
  ae_device_adapter_->ExtractAeStats(result, metadata_logger_);

  if (ShouldRunAe(result->frame_number())) {
    MaybeRunAE(result->frame_number());
  }
}

void HdrNetAeControllerImpl::SetOptions(const Options& options) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (options.enabled) {
    enabled_ = *options.enabled;
  }

  if (options.ae_frame_interval) {
    const int ae_frame_interval = *options.ae_frame_interval;
    if (ae_frame_interval > 0) {
      ae_frame_interval_ = ae_frame_interval;
    } else {
      LOGF(ERROR) << "Invalid AE frame interval: " << ae_frame_interval;
    }
  }

  if (options.max_hdr_ratio) {
    max_hdr_ratio_ = std::move(*options.max_hdr_ratio);
  }

  if (options.use_cros_face_detector) {
    use_cros_face_detector_ = *options.use_cros_face_detector;
  }

  if (options.fd_frame_interval) {
    const int fd_frame_interval = *options.fd_frame_interval;
    if (fd_frame_interval > 0) {
      fd_frame_interval_ = fd_frame_interval;
    } else {
      LOGF(ERROR) << "Invalid FD frame interval: " << fd_frame_interval;
    }
  }

  if (options.ae_stats_input_mode) {
    ae_stats_input_mode_ = *options.ae_stats_input_mode;
  }

  if (options.ae_override_mode) {
    ae_override_mode_ = *options.ae_override_mode;
  }

  if (options.exposure_compensation) {
    base_exposure_compensation_ = *options.exposure_compensation;
  }

  if (options.metadata_logger) {
    metadata_logger_ = *options.metadata_logger;
  }
}

float HdrNetAeControllerImpl::GetCalculatedHdrRatio(int frame_number) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!enabled_) {
    return 0;
  }

  base::Optional<const AeFrameInfo*> frame_info =
      GetAeFrameInfoEntry(frame_number);
  if (!frame_info) {
    // This shouldn't happen in practice, as we should always capture the result
    // metadata first before we get the HDR ratio for rendering. This is just a
    // failsafe.
    return latest_hdr_ratio_;
  }

  // The exposure may not be exactly what we wanted, so adjust the HDR ratio
  // accordingly based on the actual TET of the frame.
  float targeted_short_tet = (*frame_info)->targeted_short_tet;
  float targeted_long_tet = (*frame_info)->targeted_long_tet;
  float actual_exp_time = (*frame_info)->exposure_time_ms;
  float actual_analog_gain = (*frame_info)->analog_gain;
  float actual_digital_gain = (*frame_info)->digital_gain;
  float actual_tet = actual_exp_time * actual_analog_gain * actual_digital_gain;
  VLOGFID(1, frame_number) << "short_tet: " << targeted_short_tet
                           << " long_tet: " << targeted_long_tet
                           << " actual_tet: " << actual_tet;
  if (actual_tet == 0) {
    return latest_hdr_ratio_;
  }

  float actual_hdr_ratio = targeted_long_tet / actual_tet;
  VLOGFID(1, frame_number) << "actual_hdr_ratio: " << actual_hdr_ratio;
  return std::clamp(
      actual_hdr_ratio, 1.0f,
      LookUpHdrRatio(max_hdr_ratio_, actual_analog_gain * actual_digital_gain));
}

bool HdrNetAeControllerImpl::WriteRequestAeParameters(
    Camera3CaptureDescriptor* request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!enabled_) {
    return false;
  }

  if (!ae_device_adapter_->WriteRequestParameters(request)) {
    LOGFID(ERROR, request->frame_number()) << "Cannot set request parameters";
    return false;
  }

  if (!latest_ae_parameters_.IsValid()) {
    return false;
  }

  AeFrameInfo& frame_info =
      GetOrCreateAeFrameInfoEntry(request->frame_number());
  frame_info.targeted_short_tet = latest_ae_parameters_.short_tet;
  frame_info.targeted_long_tet = latest_ae_parameters_.long_tet;

  frame_info.targeted_ae_compensation = base_exposure_compensation_;
  base::span<const int32_t> ae_comp =
      request->GetMetadata<int32_t>(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION);
  if (!ae_comp.empty()) {
    frame_info.targeted_ae_compensation += ae_comp[0] * ae_compensation_step_;
  }

  if (use_cros_face_detector_) {
    // TODO(jcliang): Restore the metadata to the original value in capture
    // results if we end up needing this for production.
    std::array<uint8_t, 1> face_detect_mode{
        ANDROID_STATISTICS_FACE_DETECT_MODE_OFF};
    if (!request->UpdateMetadata<uint8_t>(ANDROID_STATISTICS_FACE_DETECT_MODE,
                                          face_detect_mode)) {
      LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_DETECT_MODE to OFF";
    }
  }

  // TODO(jcliang): By overriding the AE parameters here we're going to upset
  // CTS. We may need to disable HDRnet for Android.
  switch (ae_override_mode_) {
    case AeOverrideMode::kWithExposureCompensation:
      return SetExposureCompensation(request);
    case AeOverrideMode::kWithManualSensorControl:
      return SetManualSensorControls(request);
    default:
      NOTREACHED() << "Invalid AeOverrideMethod";
      return true;
  }
}

bool HdrNetAeControllerImpl::WriteResultFaceRectangles(
    Camera3CaptureDescriptor* result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!use_cros_face_detector_ || latest_faces_.empty()) {
    return true;
  }
  std::vector<int32_t> face_coordinates;
  for (const auto& f : latest_faces_) {
    face_coordinates.push_back(f.x0 * active_array_dimension_.width);
    face_coordinates.push_back(f.y0 * active_array_dimension_.height);
    face_coordinates.push_back(f.x1 * active_array_dimension_.width);
    face_coordinates.push_back(f.y1 * active_array_dimension_.height);
  }
  if (!result->UpdateMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES,
                                       face_coordinates)) {
    LOGF(ERROR) << "Cannot set face rectangles";
    return false;
  }
  return true;
}

bool HdrNetAeControllerImpl::ShouldRunAe(int frame_number) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return enabled_ && (frame_number % ae_frame_interval_ == 0);
}

bool HdrNetAeControllerImpl::ShouldRunFd(int frame_number) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return enabled_ && (frame_number % fd_frame_interval_ == 0);
}

AeFrameInfo& HdrNetAeControllerImpl::GetOrCreateAeFrameInfoEntry(
    int frame_number) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int index = frame_number % frame_info_.size();
  AeFrameInfo& entry = frame_info_[index];
  if (entry.frame_number != frame_number) {
    // Clear the data of the outdated frame.
    entry = AeFrameInfo({.frame_number = frame_number,
                         .ae_stats_input_mode = ae_stats_input_mode_,
                         .use_cros_face_detector = use_cros_face_detector_,
                         .active_array_dimension = active_array_dimension_});
  }
  return entry;
}

base::Optional<const AeFrameInfo*> HdrNetAeControllerImpl::GetAeFrameInfoEntry(
    int frame_number) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int index = frame_number % frame_info_.size();
  const AeFrameInfo& entry = frame_info_[index];
  if (entry.frame_number != frame_number) {
    return base::nullopt;
  }
  return &entry;
}

void HdrNetAeControllerImpl::MaybeRunAE(int frame_number) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AeFrameInfo& frame_info = GetOrCreateAeFrameInfoEntry(frame_number);
  if (!frame_info.IsValid() || !ae_device_adapter_->HasAeStats(frame_number)) {
    return;
  }

  float max_hdr_ratio = LookUpHdrRatio(
      max_hdr_ratio_, frame_info.analog_gain * frame_info.digital_gain);
  AeParameters ae_parameters = ae_device_adapter_->ComputeAeParameters(
      frame_number, frame_info, max_hdr_ratio);

  VLOGFID(1, frame_number) << "AE parameters:"
                           << " short_tet=" << ae_parameters.short_tet
                           << " long_tet=" << ae_parameters.long_tet;
  VLOGFID(1, frame_number) << "total gain="
                           << frame_info.analog_gain * frame_info.digital_gain
                           << " max_hdr_ratio=" << max_hdr_ratio;
  // Filter the TET transition to avoid AE fluctuations or hunting.
  if (!latest_ae_parameters_.IsValid()) {
    // This is the first set of AE parameters we get.
    latest_ae_parameters_ = ae_parameters;
  } else {
    const float kFilterStrength = 0.8f;
    latest_ae_parameters_.long_tet =
        IirFilterLog2(latest_ae_parameters_.long_tet, ae_parameters.long_tet,
                      kFilterStrength);
    latest_ae_parameters_.short_tet =
        IirFilterLog2(latest_ae_parameters_.short_tet, ae_parameters.short_tet,
                      kFilterStrength);
  }

  // Compute HDR ratio and AE exposure compensation based on the filtered TETs.
  latest_hdr_ratio_ =
      latest_ae_parameters_.long_tet / latest_ae_parameters_.short_tet;
  float actual_tet = frame_info.exposure_time_ms * frame_info.analog_gain *
                     frame_info.digital_gain;
  int delta_ae_compensation = static_cast<int>(
      std::round(std::log2(latest_ae_parameters_.short_tet / actual_tet) /
                 ae_compensation_step_));
  // Taking into consideration the compensation already applied.
  latest_ae_compensation_ = ae_compensation_range_.Clamp(
      frame_info.ae_compensation + delta_ae_compensation);

  VLOGFID(1, frame_number) << "Smoothed AE parameters:"
                           << " short_tet=" << latest_ae_parameters_.short_tet
                           << " long_tet=" << latest_ae_parameters_.long_tet
                           << " hdr_ratio=" << latest_hdr_ratio_
                           << " exposure_compensation="
                           << latest_ae_compensation_;

  if (metadata_logger_) {
    metadata_logger_->Log(
        frame_number, kTagFrameWidth,
        base::checked_cast<int32_t>(active_array_dimension_.width));
    metadata_logger_->Log(
        frame_number, kTagFrameHeight,
        base::checked_cast<int32_t>(active_array_dimension_.height));
    metadata_logger_->Log(frame_number, kTagMaxHdrRatio, max_hdr_ratio);
    metadata_logger_->Log(frame_number, kTagShortTet, ae_parameters.short_tet);
    metadata_logger_->Log(frame_number, kTagLongTet, ae_parameters.long_tet);
    metadata_logger_->Log(frame_number, kTagFilteredShortTet,
                          latest_ae_parameters_.short_tet);
    metadata_logger_->Log(frame_number, kTagFilteredLongTet,
                          latest_ae_parameters_.long_tet);
    metadata_logger_->Log(frame_number, kTagFilteredExpComp,
                          latest_ae_compensation_);
    metadata_logger_->Log(frame_number, kTagHdrRatio, latest_hdr_ratio_);
  }
}

bool HdrNetAeControllerImpl::SetExposureCompensation(
    Camera3CaptureDescriptor* request) {
  std::array<int32_t, 1> exp_comp{latest_ae_compensation_};
  if (!request->UpdateMetadata<int32_t>(
          ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, exp_comp)) {
    LOGF(WARNING) << "Cannot set AE compensation in capture request";
    return false;
  }
  if (metadata_logger_) {
    metadata_logger_->Log(request->frame_number(), kTagRequestAeCompensation,
                          exp_comp[0]);
  }

  return true;
}

bool HdrNetAeControllerImpl::SetManualSensorControls(
    Camera3CaptureDescriptor* request) {
  // Cap exposure_time to 33.33 ms.
  constexpr float kMaxExposureTime = 33.33f;
  float exp_time = std::min(latest_ae_parameters_.short_tet, kMaxExposureTime);
  float gain = latest_ae_parameters_.short_tet / exp_time;
  VLOGFID(2, request->frame_number())
      << "exp_time=" << exp_time << " gain=" << gain;

  std::array<uint8_t, 1> ae_mode{ANDROID_CONTROL_AE_MODE_OFF};
  std::array<int64_t, 1> exposure_time{
      base::checked_cast<int64_t>(exp_time * 1'000'000)};
  std::array<int32_t, 1> sensitivity{sensitivity_range_.Clamp(
      base::checked_cast<int32_t>(sensitivity_range_.lower() * gain))};
  if (!request->UpdateMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE, ae_mode) ||
      !request->UpdateMetadata<int64_t>(ANDROID_SENSOR_EXPOSURE_TIME,
                                        exposure_time) ||
      !request->UpdateMetadata<int32_t>(ANDROID_SENSOR_SENSITIVITY,
                                        sensitivity)) {
    LOGF(ERROR) << "Cannot set manual sensor control parameters";
    return false;
  }

  if (metadata_logger_) {
    metadata_logger_->Log(request->frame_number(), kTagRequestExpTime,
                          exposure_time[0]);
    metadata_logger_->Log(request->frame_number(), kTagRequestSensitivity,
                          sensitivity[0]);
  }

  return true;
}

}  // namespace cros
