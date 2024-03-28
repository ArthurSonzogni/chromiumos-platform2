// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/diagnostics_stream_manipulator.h"

#include <utility>

#include <base/check.h>
#include <drm_fourcc.h>
#include <libyuv/scale.h>

#include "cros-camera/camera_buffer_manager.h"

namespace cros {

DiagnosticsStreamManipulator::DiagnosticsStreamManipulator(
    CameraDiagnosticsClient* diagnostics_client)
    : camera_buffer_manager_(cros::CameraBufferManager::GetInstance()),
      diagnostics_client_(diagnostics_client) {}

DiagnosticsStreamManipulator::~DiagnosticsStreamManipulator() {
  DCHECK(diagnostics_client_);
  diagnostics_client_->RemoveCameraSession();
}

bool DiagnosticsStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  return true;
}

bool DiagnosticsStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  // TODO(imranziad): Select a stream and add session to diagnostics client.
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
  DCHECK(diagnostics_client_);
  if (!diagnostics_client_->IsFrameAnalysisEnabled()) {
    callbacks_.result_callback.Run(std::move(result));
    return true;
  }
  // TODO(imranziad): Select and copy an output buffer.
  callbacks_.result_callback.Run(std::move(result));
  return true;
}

void DiagnosticsStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

bool DiagnosticsStreamManipulator::Flush() {
  return true;
}

}  // namespace cros
