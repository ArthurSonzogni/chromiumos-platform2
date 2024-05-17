/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_ROTATE_AND_CROP_ROTATE_AND_CROP_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_ROTATE_AND_CROP_ROTATE_AND_CROP_STREAM_MANIPULATOR_H_

#include <hardware/camera3.h>

#include <memory>
#include <string>

#include <base/containers/flat_set.h>

#include "common/resizable_cpu_buffer.h"
#include "common/still_capture_processor.h"
#include "common/stream_manipulator.h"
#include "common/stream_manipulator_helper.h"
#include "gpu/gpu_resources.h"

namespace cros {

// This StreamManipulator implements the ANDROID_SCALER_ROTATE_AND_CROP API
// introduced since Android T, and adapts to the legacy
// |camera3_stream_t::crop_rotate_scale_degrees| API that was added in ARC-P/R
// for camera app orientation compatibility (inset-portrait mode).  Depending on
// the HAL reported ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES and the
// client ARC version, it does:
//
//   HAL modes  ARC ver.  RotateAndCropSM behavior
//   ---------------------------------------------------------------------------
//   null       P, R      Bypass crop_rotate_scale_degrees
//              T         Do rotation with ROTATE_AND_CROP
//   NONE       P, R      Do rotation with crop_rotate_scale_degrees
//              T         Do rotation with ROTATE_AND_CROP
//   > NONE     P, R      Translate crop_rotate_scale_degrees to ROTATE_AND_CROP
//              T         Bypass ROTATE_AND_CROP
//
// The HAL always receive non-AUTO value resolved by the RotateAndCropSM.
//
// The client ARC version can be distinguished by:
// - P/R: ConfigureStreams() may receive non-zero |crop_rotate_scale_degrees|.
//   and ProcessCaptureRequest() receives null or AUTO ROTATE_AND_CROP mode.
// - T: ProcessCaptureRequest() receives non-AUTO ROTATE_AND_CROP mode.
//
// TODO(b/130311697): Android P/R clients don't know the ROTATE_AND_CROP
// metadata. We assume they don't touch the default ROTATE_AND_CROP value (AUTO)
// in the default request settings, or don't send it in request metadata. See if
// we can remove this assumption to meet Android API contract.
//
class RotateAndCropStreamManipulator : public StreamManipulator {
 public:
  explicit RotateAndCropStreamManipulator(
      GpuResources* gpu_resources,
      std::unique_ptr<StillCaptureProcessor> still_capture_processor,
      std::string camera_module_name,
      mojom::CameraClientType camera_client_type);
  ~RotateAndCropStreamManipulator() override;

  static bool UpdateVendorTags(VendorTagManager& vendor_tag_manager);
  static bool UpdateStaticMetadata(android::CameraMetadata* static_info,
                                   mojom::CameraClientType camera_client_type);

  // Implementations of StreamManipulator.
  bool Initialize(const camera_metadata_t* static_info,
                  Callbacks callbacks) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override;
  void Notify(camera3_notify_msg_t msg) override;
  bool Flush() override;

 private:
  void ResetBuffersOnThread();
  void OnProcessTask(ScopedProcessTask task);

  struct CaptureContext : public StreamManipulatorHelper::PrivateContext {
    uint8_t client_rc_mode = 0;
    uint8_t hal_rc_mode = 0;
    bool result_metadata_updated = false;
  };

  GpuResources* gpu_resources_ = nullptr;
  std::unique_ptr<StillCaptureProcessor> still_capture_processor_;
  std::string camera_module_name_;
  mojom::CameraClientType camera_client_type_ =
      mojom::CameraClientType::UNKNOWN;
  std::unique_ptr<StreamManipulatorHelper> helper_;

  // Fixed after Initialize().
  bool disabled_ = false;
  base::flat_set<uint8_t> hal_available_rc_modes_;

  // Per-stream-config context.
  int client_crs_degrees_ = 0;
  ResizableCpuBuffer buffer1_, buffer2_;

  CameraThread thread_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_ROTATE_AND_CROP_ROTATE_AND_CROP_STREAM_MANIPULATOR_H_
