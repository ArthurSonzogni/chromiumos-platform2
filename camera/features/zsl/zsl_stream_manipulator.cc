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
    camera3_stream_configuration_t* stream_list,
    std::vector<camera3_stream_t*>* streams) {
  zsl_enabled_ = false;
  zsl_stream_attached_ = zsl_helper_->AttachZslStream(stream_list, streams);
  if (zsl_stream_attached_) {
    zsl_stream_ = streams->back();
  }
  return true;
}

bool ZslStreamManipulator::OnConfiguredStreams(
    camera3_stream_configuration_t* stream_list) {
  if (zsl_stream_attached_) {
    if (zsl_helper_->Initialize(stream_list)) {
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
    camera_metadata_t* default_request_settings, int type) {
  if (!zsl_enabled_) {
    return true;
  }
  base::Optional<uint8_t*> enable_zsl = GetMetadata<uint8_t>(
      default_request_settings, ANDROID_CONTROL_ENABLE_ZSL);
  if (!enable_zsl) {
    LOGF(WARNING) << "Failed to add ENABLE_ZSL to template " << type;
    return false;
  }
  **enable_zsl = ANDROID_CONTROL_ENABLE_ZSL_TRUE;
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
