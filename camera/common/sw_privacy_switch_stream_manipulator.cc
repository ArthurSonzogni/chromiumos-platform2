/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/sw_privacy_switch_stream_manipulator.h"

#include <memory>
#include <string>
#include <vector>

#include <drm_fourcc.h>
#include <linux/videodev2.h>
#include <sync/sync.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/exif_utils.h"
#include "cros-camera/jpeg_compressor.h"

namespace cros {

namespace {

// Used to fill in NV12 buffer with black pixels.
void FillInFrameWithBlackPixelsNV12(ScopedMapping& mapping) {
  // TODO(b/231543984): Consider optimization by GPU.
  auto plane = mapping.plane(0);
  // Set 0 to Y values and padding.
  memset(plane.addr, 0, plane.size);

  // Set 128 to U/V values and padding.
  plane = mapping.plane(1);
  memset(plane.addr, 128, plane.size);
}

// Used to invalidate unsupported types of buffers.
void FillInFrameWithZeros(ScopedMapping& mapping) {
  for (uint32_t i = 0; i < mapping.num_planes(); ++i) {
    auto plane = mapping.plane(i);
    memset(plane.addr, 0, plane.size);
  }
}

}  // namespace

SWPrivacySwitchStreamManipulator::SWPrivacySwitchStreamManipulator(
    RuntimeOptions* runtime_options,
    CameraMojoChannelManagerToken* mojo_manager_token)
    : runtime_options_(runtime_options),
      camera_buffer_manager_(CameraBufferManager::GetInstance()),
      jpeg_compressor_(JpegCompressor::GetInstance(mojo_manager_token)) {}

bool SWPrivacySwitchStreamManipulator::Initialize(
    GpuResources* gpu_resources_,
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  return true;
}

bool SWPrivacySwitchStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool SWPrivacySwitchStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool SWPrivacySwitchStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool SWPrivacySwitchStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  return true;
}

bool SWPrivacySwitchStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  if (runtime_options_->sw_privacy_switch_state !=
      mojom::CameraPrivacySwitchState::ON) {
    return true;
  }

  std::vector<camera3_stream_buffer_t> result_buffers;
  for (auto& buffer : result->GetOutputBuffers()) {
    buffer_handle_t handle = *buffer.buffer;
    bool is_success = true;
    auto mapping = ScopedMapping(handle);
    constexpr int kSyncWaitTimeoutMs = 300;
    if (mapping.is_valid() &&
        (buffer.release_fence == -1 ||
         !sync_wait(buffer.release_fence, kSyncWaitTimeoutMs))) {
      switch (mapping.drm_format()) {
        case DRM_FORMAT_NV12:
          FillInFrameWithBlackPixelsNV12(mapping);
          break;
        case DRM_FORMAT_R8:  // JPEG.
          is_success = FillInFrameWithBlackJpegImage(
              handle, mapping, buffer.stream->width, buffer.stream->height);
          break;
        default:
          FillInFrameWithZeros(mapping);
          is_success = false;
          break;
      }
    } else {
      is_success = false;
    }

    if (is_success) {
      result_buffers.push_back(buffer);
    } else {
      LOGF(WARNING) << "Unsupported format "
                    << FormatToString(mapping.drm_format());
      result_buffers.push_back(
          camera3_stream_buffer_t{.stream = buffer.stream,
                                  .buffer = buffer.buffer,
                                  .status = CAMERA3_BUFFER_STATUS_ERROR,
                                  .acquire_fence = -1,
                                  .release_fence = -1});
    }
  }
  result->SetOutputBuffers(result_buffers);

  return true;
}

bool SWPrivacySwitchStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  return true;
}

bool SWPrivacySwitchStreamManipulator::Flush() {
  return true;
}

bool SWPrivacySwitchStreamManipulator::FillInFrameWithBlackJpegImage(
    buffer_handle_t handle,
    ScopedMapping& mapping,
    const int width,
    const int height) {
  // TODO(b/231543984): Consider optimization by directly filling in a black
  // JPEG image possibly by GPU.
  ExifUtils utils;
  if (!utils.Initialize()) {
    LOGF(ERROR) << "Failed to initialize ExifUtils";
    return false;
  }
  if (!utils.SetImageWidth(width) || !utils.SetImageLength(height)) {
    LOGF(ERROR) << "Failed to set image resolution";
    return false;
  }

  constexpr uint32_t kBufferUsage =
      GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_VIDEO_ENCODER;
  auto in_handle = camera_buffer_manager_->AllocateScopedBuffer(
      width, height, HAL_PIXEL_FORMAT_YCbCr_420_888, kBufferUsage);
  auto in_mapping = ScopedMapping(*in_handle);
  FillInFrameWithBlackPixelsNV12(in_mapping);

  std::vector<uint8_t> empty_thumbnail;
  if (!utils.GenerateApp1(empty_thumbnail.data(), empty_thumbnail.size())) {
    LOGF(ERROR) << "Failed to generate APP1 segment";
    return false;
  }

  // We do not care about image quality for black frames, so use minimum value
  // 1 here.
  constexpr int kImageQuality = 1;
  uint32_t jpeg_data_size;
  if (!jpeg_compressor_->CompressImageFromHandle(
          *in_handle, handle, width, height, kImageQuality,
          utils.GetApp1Buffer(), utils.GetApp1Length(), &jpeg_data_size)) {
    LOGF(ERROR) << "Failed to compress JPEG image";
    return false;
  }

  auto plane = mapping.plane(0);
  camera3_jpeg_blob_t blob;
  blob.jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
  blob.jpeg_size = jpeg_data_size;
  memcpy(plane.addr + plane.size - sizeof(blob), &blob, sizeof(blob));

  return true;
}

}  // namespace cros
