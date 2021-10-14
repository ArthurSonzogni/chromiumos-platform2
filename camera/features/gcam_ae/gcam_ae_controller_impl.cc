/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/gcam_ae/gcam_ae_controller_impl.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"

namespace cros {

namespace {

// The AE compensation delta range in stops limiting the amount of AE
// compensation step changes in each frame. This can be tuned to avoid large
// fluctuations in AE compensation which can lead to severe AE instability.
constexpr float kAeCompensationDeltaStopRange[] = {-0.2f, 0.2f};

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

bool IsClientManualSensorControlSet(const AeFrameInfo& frame_info) {
  if (frame_info.client_request_settings.ae_mode &&
      frame_info.client_request_settings.ae_mode.value() ==
          ANDROID_CONTROL_AE_MODE_OFF) {
    return true;
  }
  return false;
}

std::vector<NormalizedRect> RectToNormalizedRect(
    const std::vector<Rect<float>>& faces) {
  std::vector<NormalizedRect> result;
  for (const auto& f : faces) {
    result.push_back(NormalizedRect{
        .x0 = f.left, .x1 = f.right(), .y0 = f.top, .y1 = f.bottom()});
  }
  return result;
}

}  // namespace

// static
std::unique_ptr<GcamAeController> GcamAeControllerImpl::CreateInstance(
    const camera_metadata_t* static_info) {
  return std::make_unique<GcamAeControllerImpl>(
      static_info, GcamAeDeviceAdapter::CreateInstance());
}

GcamAeControllerImpl::GcamAeControllerImpl(
    const camera_metadata_t* static_info,
    std::unique_ptr<GcamAeDeviceAdapter> ae_device_adapter)
    : ae_device_adapter_(std::move(ae_device_adapter)) {
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
  max_total_gain_ = static_cast<float>(sensitivity_range_.upper()) /
                    sensitivity_range_.lower();
  ae_compensation_step_ = (static_cast<float>(ae_compensation_step->numerator) /
                           ae_compensation_step->denominator);
  ae_compensation_range_ =
      Range<float>(ae_compensation_range[0], ae_compensation_range[1]);
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);

  ae_compensation_step_delta_range_ =
      Range<float>(kAeCompensationDeltaStopRange[0] / ae_compensation_step_,
                   kAeCompensationDeltaStopRange[1] / ae_compensation_step_);
}

void GcamAeControllerImpl::RecordYuvBuffer(int frame_number,
                                           buffer_handle_t buffer,
                                           base::ScopedFD acquire_fence) {
  if (ae_stats_input_mode_ != AeStatsInputMode::kFromYuvImage) {
    return;
  }
  AeFrameInfo* frame_info = GetAeFrameInfoEntry(frame_number);
  if (!frame_info) {
    return;
  }
  frame_info->yuv_buffer = buffer;
  frame_info->acquire_fence = std::move(acquire_fence);
  MaybeRunAE(frame_number);
}

void GcamAeControllerImpl::RecordAeMetadata(Camera3CaptureDescriptor* result) {
  AeFrameInfo* frame_info = GetAeFrameInfoEntry(result->frame_number());
  if (!frame_info) {
    return;
  }

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

  float total_gain =
      base::checked_cast<float>(sensitivity[0]) / sensitivity_range_.lower();
  float analog_gain = std::min(total_gain, max_analog_gain_);
  float digital_gain = std::max(total_gain / max_analog_gain_, 1.0f);

  frame_info->exposure_time_ms =
      base::checked_cast<float>(exposure_time_ns[0]) / 1'000'000;
  frame_info->analog_gain = analog_gain;
  frame_info->digital_gain = digital_gain;
  frame_info->estimated_sensor_sensitivity =
      (base::checked_cast<float>(sensitivity_range_.lower()) /
       (aperture[0] * aperture[0]));
  frame_info->ae_compensation = ae_compensation[0];

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
                          frame_info->estimated_sensor_sensitivity);
    metadata_logger_->Log(result->frame_number(), kTagLensAperture,
                          aperture[0]);
    metadata_logger_->Log(result->frame_number(), kTagAeExposureCompensation,
                          ae_compensation[0]);
  }

  // Face info.
  if (!frame_info->faces) {
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
    frame_info->faces =
        base::make_optional<std::vector<NormalizedRect>>(std::move(faces));
  }
  if (metadata_logger_) {
    const int num_faces = frame_info->faces.value().size();
    std::vector<float> flattened_faces(num_faces * 4);
    for (int i = 0; i < num_faces; ++i) {
      const NormalizedRect& f = frame_info->faces.value()[i];
      const int base = i * 4;
      flattened_faces[base] = f.x0;
      flattened_faces[base + 1] = f.y0;
      flattened_faces[base + 2] = f.x1;
      flattened_faces[base + 3] = f.y1;
    }
    metadata_logger_->Log(result->frame_number(), kTagFaceRectangles,
                          base::span<const float>(flattened_faces.data(),
                                                  flattened_faces.size()));
  }

  // AWB info.
  base::span<const float> color_correction_gains =
      result->GetMetadata<float>(ANDROID_COLOR_CORRECTION_GAINS);
  if (!color_correction_gains.empty()) {
    CHECK_EQ(color_correction_gains.size(), 4);
    memcpy(frame_info->rggb_gains, color_correction_gains.data(),
           4 * sizeof(float));
    VLOGFID(2, result->frame_number())
        << "AWB gains: " << frame_info->rggb_gains[0] << ", "
        << frame_info->rggb_gains[1] << ", " << frame_info->rggb_gains[2]
        << ", " << frame_info->rggb_gains[3];
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
      frame_info->ccm[i] =
          static_cast<float>(color_correction_transform[i].numerator) /
          color_correction_transform[i].denominator;
    }
    VLOGFID(2, result->frame_number())
        << "CCM: " << frame_info->ccm[0] << ", " << frame_info->ccm[1] << ", "
        << frame_info->ccm[2] << ", " << frame_info->ccm[3] << ", "
        << frame_info->ccm[4] << ", " << frame_info->ccm[5] << ", "
        << frame_info->ccm[6] << ", " << frame_info->ccm[7] << ", "
        << frame_info->ccm[8];

  } else {
    LOGF(WARNING) << "Cannot get ANDROID_COLOR_CORRECTION_TRANSFORM";
  }

  if (metadata_logger_) {
    metadata_logger_->Log(result->frame_number(), kTagCcm,
                          color_correction_transform);
  }

  // AE stats.
  ae_device_adapter_->ExtractAeStats(result, metadata_logger_);

  MaybeRunAE(result->frame_number());
}

void GcamAeControllerImpl::SetOptions(const Options& options) {
  if (options.enabled) {
    enabled_ = *options.enabled;
    if (!enabled_) {
      ae_state_machine_.OnReset();
    }
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

base::Optional<float> GcamAeControllerImpl::GetCalculatedHdrRatio(
    int frame_number) {
  if (!enabled_) {
    return base::nullopt;
  }
  AeFrameInfo* frame_info = GetAeFrameInfoEntry(frame_number);
  if (!frame_info) {
    return base::nullopt;
  }
  if (IsClientManualSensorControlSet(*frame_info)) {
    // The client is doing manual exposure control, so let's not do too much
    // with HDRnet rendering.
    return 1.0f;
  }

  return frame_info->target_hdr_ratio;
}

void GcamAeControllerImpl::SetRequestAeParameters(
    Camera3CaptureDescriptor* request) {
  if (!enabled_) {
    return;
  }

  // Set the AE parameters that will be used to actually capture the frame.
  AeFrameInfo* frame_info = CreateAeFrameInfoEntry(request->frame_number());

  RecordClientRequestSettings(request);

  if (IsClientManualSensorControlSet(*frame_info)) {
    return;
  }

  frame_info->target_tet = ae_state_machine_.GetCaptureTet();
  frame_info->target_hdr_ratio = ae_state_machine_.GetFilteredHdrRatio();
  if (metadata_logger_) {
    metadata_logger_->Log(request->frame_number(), kTagHdrRatio,
                          frame_info->target_hdr_ratio);
  }

  frame_info->target_ae_compensation = base_exposure_compensation_;
  if (frame_info->client_request_settings.ae_exposure_compensation) {
    frame_info->target_ae_compensation +=
        frame_info->client_request_settings.ae_exposure_compensation.value() *
        ae_compensation_step_;
  }

  base::span<const int32_t> fps_range =
      request->GetMetadata<int32_t>(ANDROID_CONTROL_AE_TARGET_FPS_RANGE);
  if (!fps_range.empty()) {
    frame_info->target_fps_range = {fps_range[0], fps_range[1]};
  }

  // If the FaceDetectionStreamManipulator has set the face ROIs, use them for
  // Gcam AE instead of the ones from the vendor camera HAL.
  if (request->feature_metadata().faces) {
    frame_info->faces =
        RectToNormalizedRect(*request->feature_metadata().faces);
  }

  // Only change the metadata when the client request settings is not null.
  // This is mainly to make the CTS tests happy, as some test cases set null
  // settings and if we change that the vendor camera HAL may not handle the
  // incremental changes well.
  if (!request->has_metadata()) {
    return;
  }

  if (!ae_device_adapter_->WriteRequestParameters(request)) {
    LOGFID(ERROR, request->frame_number()) << "Cannot set request parameters";
    return;
  }

  switch (ae_override_mode_) {
    case AeOverrideMode::kWithExposureCompensation:
      SetExposureCompensation(request);
      break;
    case AeOverrideMode::kWithManualSensorControl:
      SetManualSensorControls(request);
      break;
    default:
      NOTREACHED() << "Invalid AeOverrideMethod";
  }
}

void GcamAeControllerImpl::SetResultAeMetadata(
    Camera3CaptureDescriptor* result) {
  if (!enabled_) {
    return;
  }

  AeFrameInfo* frame_info = GetAeFrameInfoEntry(result->frame_number());
  if (!frame_info || IsClientManualSensorControlSet(*frame_info)) {
    return;
  }

  if (ae_override_mode_ == AeOverrideMode::kWithManualSensorControl) {
    std::array<uint8_t, 1> ae_state = {ae_state_machine_.GetAndroidAeState()};
    if (!result->UpdateMetadata<uint8_t>(ANDROID_CONTROL_AE_STATE, ae_state)) {
      LOGF(ERROR) << "Cannot set ANDROID_CONTROL_AE_STATE";
    }
  }

  RestoreClientRequestSettings(result);
}

void GcamAeControllerImpl::MaybeRunAE(int frame_number) {
  AeFrameInfo* frame_info = GetAeFrameInfoEntry(frame_number);
  DCHECK(frame_info);
  if (!ShouldRunAe(frame_number) || !frame_info->IsValid() ||
      !ae_device_adapter_->HasAeStats(frame_number)) {
    return;
  }

  float max_hdr_ratio = LookUpHdrRatio(
      max_hdr_ratio_, frame_info->analog_gain * frame_info->digital_gain);
  VLOGFID(1, frame_info->frame_number)
      << "total gain=" << frame_info->analog_gain * frame_info->digital_gain
      << " max_hdr_ratio=" << max_hdr_ratio;
  AeParameters ae_parameters = ae_device_adapter_->ComputeAeParameters(
      frame_number, *frame_info, max_hdr_ratio);

  Range<float> tet_range = {
      1e-6, static_cast<float>((1000.0 / frame_info->target_fps_range.lower()) *
                               max_total_gain_)};
  ae_state_machine_.OnNewAeParameters({.ae_frame_info = *frame_info,
                                       .ae_parameters = ae_parameters,
                                       .tet_range = tet_range},
                                      metadata_logger_);

  // Compute AE exposure compensation based on the filtered TETs.
  float actual_tet = frame_info->exposure_time_ms * frame_info->analog_gain *
                     frame_info->digital_gain;
  float delta_ae_compensation =
      std::round(std::log2(ae_state_machine_.GetCaptureTet() / actual_tet) /
                 ae_compensation_step_);
  // Taking into consideration the compensation already applied.
  filtered_ae_compensation_steps_ = ae_compensation_range_.Clamp(
      frame_info->ae_compensation +
      ae_compensation_step_delta_range_.Clamp(delta_ae_compensation));

  VLOGFID(1, frame_number) << "Filtered AE compensation:"
                           << " hdr_ratio="
                           << ae_state_machine_.GetFilteredHdrRatio()
                           << " exposure_compensation="
                           << filtered_ae_compensation_steps_;

  if (metadata_logger_) {
    metadata_logger_->Log(
        frame_info->frame_number, kTagFrameWidth,
        base::checked_cast<int32_t>(active_array_dimension_.width));
    metadata_logger_->Log(
        frame_info->frame_number, kTagFrameHeight,
        base::checked_cast<int32_t>(active_array_dimension_.height));
    metadata_logger_->Log(frame_number, kTagMaxHdrRatio, max_hdr_ratio);
    metadata_logger_->Log(frame_number, kTagFilteredExpComp,
                          filtered_ae_compensation_steps_);
  }
}

void GcamAeControllerImpl::RecordClientRequestSettings(
    const Camera3CaptureDescriptor* request) {
  AeFrameInfo* frame_info = GetAeFrameInfoEntry(request->frame_number());
  DCHECK(frame_info);

  base::span<const uint8_t> ae_mode =
      request->GetMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE);
  if (!ae_mode.empty()) {
    frame_info->client_request_settings.ae_mode = ae_mode[0];
    VLOGFID(2, request->frame_number())
        << "Client requested ANDROID_CONTROL_AE_MODE="
        << static_cast<int>(*frame_info->client_request_settings.ae_mode);
  }

  base::span<const int32_t> ae_comp =
      request->GetMetadata<int32_t>(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION);
  if (!ae_comp.empty()) {
    frame_info->client_request_settings.ae_exposure_compensation = ae_comp[0];
    VLOGFID(2, request->frame_number())
        << "Client requested ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION="
        << static_cast<int>(
               *frame_info->client_request_settings.ae_exposure_compensation);
  }

  base::span<const uint8_t> ae_lock =
      request->GetMetadata<uint8_t>(ANDROID_CONTROL_AE_LOCK);
  if (!ae_lock.empty()) {
    frame_info->client_request_settings.ae_lock = ae_lock[0];
    VLOGFID(2, request->frame_number())
        << "Client requested ANDROID_CONTROL_AE_LOCK="
        << static_cast<int>(*frame_info->client_request_settings.ae_lock);
  }
}

void GcamAeControllerImpl::RestoreClientRequestSettings(
    Camera3CaptureDescriptor* result) {
  AeFrameInfo* frame_info = GetAeFrameInfoEntry(result->frame_number());
  DCHECK(frame_info);

  if (frame_info->client_request_settings.ae_mode) {
    std::array<uint8_t, 1> ae_mode = {
        *frame_info->client_request_settings.ae_mode};
    if (!result->UpdateMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE, ae_mode)) {
      LOGF(ERROR) << "Cannot restore ANDROID_CONTROL_AE_MODE";
    } else {
      VLOGFID(2, result->frame_number())
          << "Restored ANDROID_CONTROL_AE_MODE="
          << static_cast<int>(*frame_info->client_request_settings.ae_mode);
    }
  }

  if (frame_info->client_request_settings.ae_exposure_compensation) {
    std::array<int32_t, 1> ae_exposure_compensation = {
        *frame_info->client_request_settings.ae_exposure_compensation};
    if (!result->UpdateMetadata<int32_t>(
            ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
            ae_exposure_compensation)) {
      LOGF(ERROR) << "Cannot restore ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION";
    } else {
      VLOGFID(2, result->frame_number())
          << "Restored ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION="
          << static_cast<int>(
                 *frame_info->client_request_settings.ae_exposure_compensation);
    }
  }

  if (frame_info->client_request_settings.ae_lock) {
    std::array<uint8_t, 1> ae_lock = {
        *frame_info->client_request_settings.ae_lock};
    if (!result->UpdateMetadata<uint8_t>(ANDROID_CONTROL_AE_LOCK, ae_lock)) {
      LOGF(ERROR) << "Cannot restore ANDROID_CONTROL_AE_LOCK";
    } else {
      VLOGFID(2, result->frame_number())
          << "Restored ANDROID_CONTROL_AE_LOCK="
          << static_cast<int>(*frame_info->client_request_settings.ae_lock);
    }
  }
}

void GcamAeControllerImpl::SetExposureCompensation(
    Camera3CaptureDescriptor* request) {
  std::array<int32_t, 1> exp_comp = {
      static_cast<int32_t>(filtered_ae_compensation_steps_)};
  if (!request->UpdateMetadata<int32_t>(
          ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, exp_comp)) {
    LOGF(WARNING) << "Cannot set AE compensation in capture request";
    return;
  }
  if (metadata_logger_) {
    metadata_logger_->Log(request->frame_number(), kTagRequestAeCompensation,
                          exp_comp[0]);
  }

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number())
        << "filtered_ae_compensation_: " << filtered_ae_compensation_steps_;
    VLOGFID(2, request->frame_number())
        << "actual_ae_compensation_: " << exp_comp[0];
  }
}

void GcamAeControllerImpl::SetManualSensorControls(
    Camera3CaptureDescriptor* request) {
  AeFrameInfo* frame_info = GetAeFrameInfoEntry(request->frame_number());
  if (!frame_info->target_tet) {
    return;
  }

  const float max_exposure_time_ms =
      1000.0f / base::checked_cast<float>(frame_info->target_fps_range.lower());
  float exp_time = std::min(frame_info->target_tet, max_exposure_time_ms);
  float gain = frame_info->target_tet / exp_time;
  VLOGFID(2, request->frame_number())
      << "exp_time=" << exp_time << " gain=" << gain;

  std::array<uint8_t, 1> ae_mode = {ANDROID_CONTROL_AE_MODE_OFF};
  std::array<uint8_t, 1> ae_lock = {ANDROID_CONTROL_AE_LOCK_OFF};
  std::array<int64_t, 1> exposure_time = {
      base::checked_cast<int64_t>(exp_time * 1e6)};
  std::array<int32_t, 1> sensitivity = {sensitivity_range_.Clamp(
      base::checked_cast<int32_t>(sensitivity_range_.lower() * gain))};
  if (!request->UpdateMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE, ae_mode) ||
      !request->UpdateMetadata<uint8_t>(ANDROID_CONTROL_AE_LOCK, ae_lock) ||
      !request->UpdateMetadata<int64_t>(ANDROID_SENSOR_EXPOSURE_TIME,
                                        exposure_time) ||
      !request->UpdateMetadata<int32_t>(ANDROID_SENSOR_SENSITIVITY,
                                        sensitivity)) {
    LOGF(ERROR) << "Cannot set manual sensor control parameters";
    return;
  }

  if (metadata_logger_) {
    metadata_logger_->Log(request->frame_number(), kTagRequestExpTime,
                          exposure_time[0]);
    metadata_logger_->Log(request->frame_number(), kTagRequestSensitivity,
                          sensitivity[0]);
  }
}

bool GcamAeControllerImpl::ShouldRunAe(int frame_number) const {
  return enabled_ && (frame_number % ae_frame_interval_ == 0);
}

AeFrameInfo* GcamAeControllerImpl::CreateAeFrameInfoEntry(int frame_number) {
  int index = frame_number % frame_info_.size();
  AeFrameInfo& entry = frame_info_[index];
  if (entry.frame_number != frame_number) {
    // Clear the data of the outdated frame.
    entry = AeFrameInfo({.frame_number = frame_number,
                         .ae_stats_input_mode = ae_stats_input_mode_,
                         .active_array_dimension = active_array_dimension_});
  }
  return &entry;
}

AeFrameInfo* GcamAeControllerImpl::GetAeFrameInfoEntry(int frame_number) {
  int index = frame_number % frame_info_.size();
  if (frame_info_[index].frame_number != frame_number) {
    return nullptr;
  }
  return &frame_info_[index];
}

}  // namespace cros
