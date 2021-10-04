/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/gcam_ae/gcam_ae_stream_manipulator.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <system/camera_metadata.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "features/gcam_ae/gcam_ae_controller_impl.h"

namespace cros {

namespace {

constexpr char kMetadataDumpPath[] = "/run/camera/gcam_ae_frame_metadata.json";

constexpr char kAeFrameIntervalKey[] = "ae_frame_interval";
constexpr char kAeOverrideModeKey[] = "ae_override_mode";
constexpr char kAeStatsInputModeKey[] = "ae_stats_input_mode";
constexpr char kExposureCompensationKey[] = "exp_comp";
constexpr char kGcamAeEnableKey[] = "gcam_ae_enable";
constexpr char kLogFrameMetadataKey[] = "log_frame_metadata";
constexpr char kMaxHdrRatioKey[] = "max_hdr_ratio";

}  // namespace

//
// GcamAeStreamManipulator implementations.
//

GcamAeStreamManipulator::GcamAeStreamManipulator(
    GcamAeController::Factory gcam_ae_controller_factory)
    : config_(kDefaultGcamAeConfigFile, kOverrideGcamAeConfigFile),
      gcam_ae_controller_factory_(
          !gcam_ae_controller_factory.is_null()
              ? std::move(gcam_ae_controller_factory)
              : base::BindRepeating(GcamAeControllerImpl::CreateInstance)),
      metadata_logger_({.dump_path = base::FilePath(kMetadataDumpPath)}) {}

bool GcamAeStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  static_info_.acquire(clone_camera_metadata(static_info));
  {
    base::AutoLock lock(ae_controller_lock_);
    ae_controller_ = gcam_ae_controller_factory_.Run(static_info);
  }
  // Set the options callback here to set the latest options to
  // |ae_controller_|.
  config_.SetCallback(base::BindRepeating(
      &GcamAeStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
  return true;
}

bool GcamAeStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  yuv_stream_ = nullptr;

  for (auto* s : stream_config->GetStreams()) {
    if (s->stream_type != CAMERA3_STREAM_OUTPUT) {
      continue;
    }

    // TODO(jcliang): See if we need to support 10-bit YUV (i.e. with format
    // HAL_PIXEL_FORMAT_YCBCR_P010);
    if (s->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
        s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
      if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
          (s->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
              GRALLOC_USAGE_HW_CAMERA_ZSL) {
        // Ignore ZSL streams.
        continue;
      }

      // Pass the buffer with the largest width to AE controller. This is a
      // heuristic and shouldn't matter for the majority of the time, as for
      // most cases the requested streams would have the same aspect ratio.
      if (!yuv_stream_ || s->width > yuv_stream_->width) {
        yuv_stream_ = s;
      }
    }
  }

  if (yuv_stream_) {
    VLOGF(1) << "YUV stream for Gcam AE processing: "
             << GetDebugString(yuv_stream_);
  } else {
    LOGF(WARNING) << "No YUV stream suitable for Gcam AE processing";
  }

  return true;
}

bool GcamAeStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool GcamAeStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool GcamAeStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  if (request->GetInputBuffer() != nullptr) {
    // Skip reprocessing requests.
    return true;
  }
  base::AutoLock lock(ae_controller_lock_);
  ae_controller_->SetRequestAeParameters(request);
  return true;
}

bool GcamAeStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result->frame_number()) << "Got result:";
    for (const auto& hal_result_buffer : result->GetOutputBuffers()) {
      VLOGF(2) << "\t" << GetDebugString(hal_result_buffer.stream);
    }
  }

  base::AutoLock lock(ae_controller_lock_);
  if (result->has_metadata()) {
    ae_controller_->RecordAeMetadata(result);
    ae_controller_->SetResultAeMetadata(result);
  }

  // Pass along the calculated HDR ratio to HdrNetStreamManipulator for HDRnet
  // rendering.
  result->feature_metadata().hdr_ratio =
      ae_controller_->GetCalculatedHdrRatio(result->frame_number());

  if (result->num_output_buffers() == 0) {
    return true;
  }

  for (auto& buffer : result->GetOutputBuffers()) {
    if (buffer.stream == yuv_stream_) {
      ae_controller_->RecordYuvBuffer(result->frame_number(), *buffer.buffer,
                                      base::ScopedFD());
    }
  }

  return true;
}

bool GcamAeStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  return true;
}

bool GcamAeStreamManipulator::Flush() {
  return true;
}

void GcamAeStreamManipulator::OnOptionsUpdated(const base::Value& json_values) {
  auto gcam_ae_enable = json_values.FindBoolKey(kGcamAeEnableKey);
  if (gcam_ae_enable) {
    options_.gcam_ae_enable = *gcam_ae_enable;
  }
  auto ae_frame_interval = json_values.FindIntKey(kAeFrameIntervalKey);
  if (ae_frame_interval) {
    options_.ae_frame_interval = *ae_frame_interval;
  }
  auto max_hdr_ratio = json_values.FindDictKey(kMaxHdrRatioKey);
  if (max_hdr_ratio) {
    base::flat_map<float, float> hdr_ratio_map;
    for (auto [k, v] : max_hdr_ratio->DictItems()) {
      double gain;
      if (!base::StringToDouble(k, &gain)) {
        LOGF(ERROR) << "Invalid gain value: " << k;
        continue;
      }
      base::Optional<double> ratio = v.GetIfDouble();
      if (!ratio) {
        LOGF(ERROR) << "Invalid max_hdr_ratio";
        continue;
      }
      hdr_ratio_map.insert({gain, *ratio});
    }
    options_.max_hdr_ratio = std::move(hdr_ratio_map);
  }
  auto ae_stats_input_mode = json_values.FindIntKey(kAeStatsInputModeKey);
  if (ae_stats_input_mode) {
    if (*ae_stats_input_mode ==
            static_cast<int>(AeStatsInputMode::kFromVendorAeStats) ||
        *ae_stats_input_mode ==
            static_cast<int>(AeStatsInputMode::kFromYuvImage)) {
      options_.ae_stats_input_mode =
          static_cast<AeStatsInputMode>(*ae_stats_input_mode);
    } else {
      LOGF(ERROR) << "Invalid AE stats input mode: " << *ae_stats_input_mode;
    }
  }
  auto ae_override_method = json_values.FindIntKey(kAeOverrideModeKey);
  if (ae_override_method) {
    if (*ae_override_method ==
            static_cast<int>(AeOverrideMode::kWithExposureCompensation) ||
        *ae_override_method ==
            static_cast<int>(AeOverrideMode::kWithManualSensorControl)) {
      options_.ae_override_mode =
          static_cast<AeOverrideMode>(*ae_override_method);
    } else {
      LOGF(ERROR) << "Invalid AE override method: " << *ae_override_method;
    }
  }
  auto exp_comp = json_values.FindDoubleKey(kExposureCompensationKey);
  if (exp_comp) {
    options_.exposure_compensation = *exp_comp;
  }
  auto log_frame_metadata = json_values.FindBoolKey(kLogFrameMetadataKey);
  if (log_frame_metadata) {
    if (options_.log_frame_metadata && !log_frame_metadata.value()) {
      // Dump frame metadata when metadata logging if turned off.
      metadata_logger_.DumpMetadata();
      metadata_logger_.Clear();
    }
    options_.log_frame_metadata = *log_frame_metadata;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Gcam AE config:"
             << " gcam_ae_enable=" << options_.gcam_ae_enable
             << " ae_frame_interval=" << options_.ae_frame_interval
             << " ae_stats_input_mode="
             << static_cast<int>(options_.ae_stats_input_mode)
             << " exposure_compensation=" << options_.exposure_compensation
             << " log_frame_metadata=" << options_.log_frame_metadata;
    VLOGF(1) << "max_hdr_ratio:";
    for (auto [gain, ratio] : options_.max_hdr_ratio) {
      VLOGF(1) << "  " << gain << ": " << ratio;
    }
  }

  base::AutoLock lock(ae_controller_lock_);
  if (ae_controller_) {
    ae_controller_->SetOptions({
        .enabled = options_.gcam_ae_enable,
        .ae_frame_interval = options_.ae_frame_interval,
        .max_hdr_ratio = options_.max_hdr_ratio,
        .ae_stats_input_mode = options_.ae_stats_input_mode,
        .ae_override_mode = options_.ae_override_mode,
        .exposure_compensation = options_.exposure_compensation,
        .metadata_logger =
            options_.log_frame_metadata ? &metadata_logger_ : nullptr,
    });
  }
}

}  // namespace cros
