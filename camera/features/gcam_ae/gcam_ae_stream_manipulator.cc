/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/gcam_ae/gcam_ae_stream_manipulator.h"

#include <utility>

#include <system/camera_metadata.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "features/gcam_ae/gcam_ae_controller_impl.h"

namespace cros {

namespace {

constexpr char kMetadataDumpPath[] = "/run/camera/gcam_ae_frame_metadata.json";

constexpr char kGcamAeEnableKey[] = "gcam_ae_enable";
constexpr char kLogFrameMetadataKey[] = "log_frame_metadata";

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
  LoadIfExist(json_values, kGcamAeEnableKey, &options_.gcam_ae_enable);

  bool log_frame_metadata;
  if (LoadIfExist(json_values, kLogFrameMetadataKey, &log_frame_metadata)) {
    if (options_.log_frame_metadata && !log_frame_metadata) {
      // Dump frame metadata when metadata logging if turned off.
      metadata_logger_.DumpMetadata();
      metadata_logger_.Clear();
    }
    options_.log_frame_metadata = log_frame_metadata;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Gcam AE config:"
             << " gcam_ae_enable=" << options_.gcam_ae_enable
             << " log_frame_metadata=" << options_.log_frame_metadata;
  }

  base::AutoLock lock(ae_controller_lock_);
  if (ae_controller_) {
    ae_controller_->OnOptionsUpdated(
        json_values, options_.log_frame_metadata ? &metadata_logger_ : nullptr);
  }
}

}  // namespace cros
