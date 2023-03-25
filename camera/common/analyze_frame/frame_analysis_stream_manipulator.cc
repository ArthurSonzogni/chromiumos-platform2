// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <drm_fourcc.h>
#include <libyuv.h>
#include <libyuv/convert_argb.h>
#include <libyuv/scale.h>
#include <linux/videodev2.h>
#include <sync/sync.h>
#include <system/graphics-base-v1.0.h>

#include "common/analyze_frame/frame_analysis_stream_manipulator.h"
#include "common/camera_hal3_helpers.h"

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_buffer_utils.h"
#include "cutils/native_handle.h"

namespace cros {

constexpr uint32_t kFrameCopyInterval = 27;

bool FrameAnalysisStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  return true;
}

bool FrameAnalysisStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool FrameAnalysisStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool FrameAnalysisStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool FrameAnalysisStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  return true;
}

bool FrameAnalysisStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  if (result.frame_number() % kFrameCopyInterval != 0) {
    callbacks_.result_callback.Run(std::move(result));
    return true;
  }

  buffer_handle_t handle = nullptr;

  for (auto& stream_buffer : result.GetMutableOutputBuffers()) {
    constexpr int kSyncWaitTimeoutMs = 300;
    if (!stream_buffer.WaitOnAndClearReleaseFence(kSyncWaitTimeoutMs)) {
      LOGF(ERROR) << "Timed out waiting for acquiring output buffer";
      stream_buffer.mutable_raw_buffer().status = CAMERA3_BUFFER_STATUS_ERROR;
      continue;
    }
    handle = *stream_buffer.buffer();
    auto mapping_src = ScopedMapping(handle);
    if (mapping_src.is_valid() && mapping_src.drm_format() == DRM_FORMAT_NV12) {
      ProcessBuffer(mapping_src);
      break;
    }
  }
  if (!handle) {
    LOGF(WARNING) << "Valid output buffer not found for frame number:"
                  << result.frame_number();
  }

  callbacks_.result_callback.Run(std::move(result));
  return true;
}

void FrameAnalysisStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

bool FrameAnalysisStreamManipulator::Flush() {
  return true;
}

void FrameAnalysisStreamManipulator::ProcessBuffer(ScopedMapping& mapping_src) {
  constexpr uint32_t kBufferUsage =
      GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;

  uint32_t src_width = mapping_src.width();
  uint32_t src_height = mapping_src.height();

  const float kAspectRatioMargin = 0.04;
  const float kTargetAspectRatio16_9 = 1.778;
  const float kTargetAspectRatio4_3 = 1.333;

  float aspect_ratio =
      static_cast<float>(src_width) / static_cast<float>(src_height);

  uint32_t target_width, target_height;

  if (std::fabs(kTargetAspectRatio16_9 - aspect_ratio) < kAspectRatioMargin) {
    target_width = 640;
    target_height = 360;
  } else if (std::fabs(kTargetAspectRatio4_3 - aspect_ratio) <
             kAspectRatioMargin) {
    target_width = 640;
    target_height = 480;
  } else {
    LOGF(WARNING) << "aspect ratio does not match";
    return;
  }

  // scaling step
  ScopedBufferHandle scoped_handle = CameraBufferManager::AllocateScopedBuffer(
      target_width, target_height, mapping_src.hal_pixel_format(),
      kBufferUsage);
  buffer_handle_t scaled_buffer = *scoped_handle;
  auto mapping_scaled = ScopedMapping(scaled_buffer);
  int ret = libyuv::NV12Scale(
      mapping_src.plane(0).addr, mapping_src.plane(0).stride,
      mapping_src.plane(1).addr, mapping_src.plane(1).stride, src_width,
      src_height, mapping_scaled.plane(0).addr, mapping_scaled.plane(0).stride,
      mapping_scaled.plane(1).addr, mapping_scaled.plane(1).stride,
      target_width, target_height, libyuv::kFilterBilinear);

  if (ret != 0) {
    LOGF(ERROR) << "libyuv::NV12Scale() failed: " << ret;
  }
  // TODO(rabbim): Dispatch |scaled_buffer|
}
}  // namespace cros
