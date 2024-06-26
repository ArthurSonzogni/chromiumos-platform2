/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "camera/features/kiosk_vision/kiosk_vision_stream_manipulator.h"

#include <utility>
#include <ml_core/dlc/dlc_ids.h>

#include "camera/features/kiosk_vision/tracing.h"

namespace cros {

KioskVisionStreamManipulator::KioskVisionStreamManipulator(
    RuntimeOptions* runtime_options)
    : dlc_path_(runtime_options->GetDlcRootPath(dlc_client::kKioskVisionDlcId)),
      observer_(runtime_options->GetKioskVisionObserver()) {
  LOGF(INFO) << "KioskVisionStreamManipulator is created";
}

KioskVisionStreamManipulator::~KioskVisionStreamManipulator() = default;

bool KioskVisionStreamManipulator::Initialize(
    const camera_metadata_t* static_info, Callbacks callbacks) {
  TRACE_KIOSK_VISION();
  callbacks_ = std::move(callbacks);
  return true;
}

bool KioskVisionStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  TRACE_KIOSK_VISION();
  return true;
}

bool KioskVisionStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  TRACE_KIOSK_VISION();
  return true;
}

bool KioskVisionStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  TRACE_KIOSK_VISION();
  return true;
}

bool KioskVisionStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  TRACE_KIOSK_VISION("frame_number", request->frame_number());
  return true;
}

bool KioskVisionStreamManipulator::Flush() {
  return true;
}

bool KioskVisionStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  TRACE_KIOSK_VISION("frame_number", result.frame_number());

  base::ScopedClosureRunner result_callback_task =
      StreamManipulator::MakeScopedCaptureResultCallbackRunner(
          callbacks_.result_callback, result);

  // TODO(b/339396282): call kiosk_vision_wrapper_->ProcessFrame();
  return true;
}

void KioskVisionStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

const base::FilePath& KioskVisionStreamManipulator::GetDlcPathForTesting()
    const {
  return dlc_path_;
}

}  // namespace cros
