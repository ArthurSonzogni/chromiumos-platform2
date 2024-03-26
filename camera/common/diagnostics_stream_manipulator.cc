// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/diagnostics_stream_manipulator.h"

#include <cstdint>
#include <memory>
#include <utility>

#include <drm_fourcc.h>
#include <libyuv/scale.h>

#include "cros-camera/camera_buffer_manager.h"

namespace cros {

constexpr uint32_t kFrameCopyInterval = 27;

DiagnosticsStreamManipulator::DiagnosticsStreamManipulator(
    CameraDiagnosticsConfig* diagnostics_config)
    : camera_buffer_manager_(cros::CameraBufferManager::GetInstance()),
      diagnostics_config_(diagnostics_config) {}

bool DiagnosticsStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  return true;
}

bool DiagnosticsStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool DiagnosticsStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool DiagnosticsStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool DiagnosticsStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  return true;
}

bool DiagnosticsStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  DCHECK(diagnostics_config_);
  if (!diagnostics_config_->IsFrameInterceptorEnabled() ||
      result.frame_number() % kFrameCopyInterval != 0) {
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

void DiagnosticsStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

bool DiagnosticsStreamManipulator::Flush() {
  return true;
}

void DiagnosticsStreamManipulator::ProcessBuffer(ScopedMapping& mapping_src) {
  // TODO(imranziad): Add new implementation in follow up CL.
}

}  // namespace cros
