/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator.h"

#include <utility>

#include <base/files/file_util.h>

#include "common/framing_stream_manipulator.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "features/rotate_and_crop/rotate_and_crop_stream_manipulator.h"
#include "features/zsl/zsl_stream_manipulator.h"
#if USE_CAMERA_FEATURE_PORTRAIT_MODE
#include "features/portrait_mode/portrait_mode_stream_manipulator.h"
#endif

namespace cros {

void StreamManipulator::RuntimeOptions::SetAutoFramingState(
    mojom::CameraAutoFramingState state) {
  base::AutoLock lock(lock_);
  auto_framing_state_ = state;
}

mojom::CameraAutoFramingState
StreamManipulator::RuntimeOptions::GetAutoFramingState() {
  base::AutoLock lock(lock_);
  return auto_framing_state_;
}

void StreamManipulator::RuntimeOptions::SetSWPrivacySwitchState(
    mojom::CameraPrivacySwitchState state) {
  {
    base::AutoLock lock(lock_);
    LOGF(INFO) << "SW privacy switch state changed from "
               << sw_privacy_switch_state_ << " to " << state;
    sw_privacy_switch_state_ = state;
  }
}

void StreamManipulator::RuntimeOptions::SetEffectsConfig(
    mojom::EffectsConfigPtr config) {
  base::AutoLock lock(lock_);
  effects_config_ = std::move(config);
}

mojom::EffectsConfigPtr StreamManipulator::RuntimeOptions::GetEffectsConfig() {
  base::AutoLock lock(lock_);
  // Return a copy.
  return effects_config_->Clone();
}

base::FilePath StreamManipulator::RuntimeOptions::GetDlcRootPath(
    const std::string& dlc_id) const {
  base::AutoLock lock(lock_);
  auto it = dlc_root_paths_.find(dlc_id);
  if (it != dlc_root_paths_.end()) {
    return it->second;
  }
  return base::FilePath();
}

void StreamManipulator::RuntimeOptions::SetDlcRootPath(
    const std::string& dlc_id, const base::FilePath& path) {
  base::AutoLock lock(lock_);
  dlc_root_paths_[dlc_id] = path;
}

mojom::CameraAutoFramingState
StreamManipulator::RuntimeOptions::auto_framing_state() {
  base::AutoLock lock(lock_);
  return auto_framing_state_;
}

mojom::CameraPrivacySwitchState
StreamManipulator::RuntimeOptions::sw_privacy_switch_state() {
  base::AutoLock lock(lock_);
  return sw_privacy_switch_state_;
}

// static
bool StreamManipulator::UpdateVendorTags(VendorTagManager& vendor_tag_manager) {
  if (!ZslStreamManipulator::UpdateVendorTags(vendor_tag_manager) ||
      !RotateAndCropStreamManipulator::UpdateVendorTags(vendor_tag_manager) ||
      !FramingStreamManipulator::UpdateVendorTags(vendor_tag_manager)) {
    return false;
  }
#if USE_CAMERA_FEATURE_PORTRAIT_MODE
  if (!PortraitModeStreamManipulator::UpdateVendorTags(vendor_tag_manager)) {
    return false;
  }
#endif
  return true;
}

// static
bool StreamManipulator::UpdateStaticMetadata(
    android::CameraMetadata* static_info, mojom::CameraClientType client_type) {
  if (!ZslStreamManipulator::UpdateStaticMetadata(static_info) ||
      !RotateAndCropStreamManipulator::UpdateStaticMetadata(static_info,
                                                            client_type) ||
      !FramingStreamManipulator::UpdateStaticMetadata(static_info)) {
    return false;
  }
#if USE_CAMERA_FEATURE_PORTRAIT_MODE
  if (!PortraitModeStreamManipulator::UpdateStaticMetadata(static_info)) {
    return false;
  }
#endif
  return true;
}

scoped_refptr<base::SingleThreadTaskRunner> StreamManipulator::GetTaskRunner() {
  return nullptr;
}

// static
base::ScopedClosureRunner
StreamManipulator::MakeScopedCaptureResultCallbackRunner(
    CaptureResultCallback& result_callback, Camera3CaptureDescriptor& result) {
  return base::ScopedClosureRunner(base::BindOnce(
      [](StreamManipulator::CaptureResultCallback& cb,
         Camera3CaptureDescriptor& result) { cb.Run(std::move(result)); },
      std::ref(result_callback), std::ref(result)));
}

}  // namespace cros
