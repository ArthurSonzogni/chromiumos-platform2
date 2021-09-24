/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/gcam_ae/gcam_ae_stream_manipulator.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <system/camera_metadata.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "features/gcam_ae/gcam_ae_controller_impl.h"

namespace cros {

namespace {

constexpr char kMetadataDumpPath[] = "/run/camera/gcam_ae_frame_metadata.json";

}  // namespace

//
// GcamAeStreamManipulator implementations.
//

GcamAeStreamManipulator::GcamAeStreamManipulator(
    GcamAeController::Factory gcam_ae_controller_factory)
    : gcam_ae_controller_factory_(
          !gcam_ae_controller_factory.is_null()
              ? std::move(gcam_ae_controller_factory)
              : base::BindRepeating(GcamAeControllerImpl::CreateInstance)) {}

bool GcamAeStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  static_info_.acquire(clone_camera_metadata(static_info));
  base::AutoLock lock(ae_controller_lock_);
  ae_controller_ = gcam_ae_controller_factory_.Run(static_info);
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

  GcamAeConfig::Options options = config_.GetOptions();
  if (options.log_frame_metadata) {
    if (!metadata_logger_) {
      metadata_logger_ =
          std::make_unique<MetadataLogger>(MetadataLogger::Options{
              .dump_path = base::FilePath(kMetadataDumpPath)});
    }
  } else {
    metadata_logger_ = nullptr;
  }
  base::AutoLock lock(ae_controller_lock_);
  ae_controller_->SetOptions({
      .enabled = options.gcam_ae_enable,
      .ae_frame_interval = options.ae_frame_interval,
      .max_hdr_ratio = options.max_hdr_ratio,
      .use_cros_face_detector = options.use_cros_face_detector,
      .fd_frame_interval = options.fd_frame_interval,
      .ae_stats_input_mode = options.ae_stats_input_mode,
      .ae_override_mode = options.ae_override_mode,
      .exposure_compensation = options.exposure_compensation,
      .metadata_logger = metadata_logger_.get(),
  });

  ae_controller_->WriteRequestAeParameters(request);

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
    GcamAeConfig::Options options = config_.GetOptions();
    ae_controller_->RecordAeMetadata(result);

    if (options.use_cros_face_detector) {
      // This is mainly for displaying the face rectangles in camera app for
      // development and debugging.
      ae_controller_->WriteResultFaceRectangles(result);
    }
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

}  // namespace cros
