/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/zsl/zsl_stream_manipulator.h"

#include "cros-camera/camera_metadata_utils.h"

namespace cros {

ZslStreamManipulator::ZslStreamManipulator() {}

ZslStreamManipulator::~ZslStreamManipulator() {}

bool ZslStreamManipulator::Initialize(const camera_metadata_t* static_info) {
  base::Optional<int32_t> partial_result_count =
      GetRoMetadata<int32_t>(static_info, ANDROID_REQUEST_PARTIAL_RESULT_COUNT);
  if (!partial_result_count) {
    LOGF(ERROR) << "Cannot find ANDROID_REQUEST_PARTIAL_RESULT_COUNT in static "
                   "metadata";
    return false;
  }
  partial_result_count_ = *partial_result_count;
  zsl_helper_ = std::make_unique<ZslHelper>(static_info);
  return true;
}

bool ZslStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  zsl_enabled_ = false;
  zsl_stream_attached_ = zsl_helper_->AttachZslStream(stream_config);
  if (zsl_stream_attached_) {
    zsl_stream_ = stream_config->GetStreams().back();
  }
  return true;
}

bool ZslStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  if (zsl_stream_attached_) {
    if (zsl_helper_->Initialize(stream_config)) {
      zsl_enabled_ = true;
      LOGF(INFO) << "Enabling ZSL";
    } else {
      LOGF(ERROR) << "Failed to initialize ZslHelper";
      return false;
    }
  }
  return true;
}

bool ZslStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  uint8_t zsl_enable = ANDROID_CONTROL_ENABLE_ZSL_TRUE;
  if (default_request_settings->update(ANDROID_CONTROL_ENABLE_ZSL, &zsl_enable,
                                       1) != 0) {
    LOGF(WARNING) << "Failed to add ENABLE_ZSL to template " << type;
    return false;
  }
  LOGF(INFO) << "Added ENABLE_ZSL to template " << type;
  return true;
}

bool ZslStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  if (zsl_enabled_) {
    zsl_helper_->ProcessZslCaptureRequest(
        request, ZslHelper::SelectionStrategy::CLOSEST_3A);
  }

  // We add ANDROID_CONTROL_ENABLE_ZSL to the capture templates. We need to make
  // sure it is hidden from the actual HAL.
  request->DeleteMetadata(ANDROID_CONTROL_ENABLE_ZSL);

  return true;
}

bool ZslStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  bool is_input_transformed = false;
  if (zsl_enabled_) {
    zsl_helper_->ProcessZslCaptureResult(result, &is_input_transformed);
  }
  // If we attempt ZSL, we'll add ANDROID_CONTROL_ENABLE_ZSL to the capture
  // template which will then require us to add it to capture results as well.
  if (result->partial_result() == partial_result_count_) {
    result->UpdateMetadata<uint8_t>(
        ANDROID_CONTROL_ENABLE_ZSL,
        std::array<uint8_t, 1>{ANDROID_CONTROL_ENABLE_ZSL_TRUE});
  }

  return true;
}

bool ZslStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  if (msg->type == CAMERA3_MSG_ERROR) {
    zsl_helper_->OnNotifyError(msg->message.error);
  }
  return true;
}

bool ZslStreamManipulator::Flush() {
  return true;
}

}  // namespace cros
