/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_SW_PRIVACY_SWITCH_STREAM_MANIPULATOR_H_
#define CAMERA_COMMON_SW_PRIVACY_SWITCH_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <memory>

#include <hardware/camera3.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_mojo_channel_manager_token.h"
#include "cros-camera/jpeg_compressor.h"

namespace cros {

class SWPrivacySwitchStreamManipulator : public StreamManipulator {
 public:
  SWPrivacySwitchStreamManipulator(
      RuntimeOptions* runtime_options,
      CameraMojoChannelManagerToken* mojo_manager_token);

  // Implementations of StreamManipulator.
  bool Initialize(const camera_metadata_t* static_info,
                  CaptureResultCallback result_callback) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor* result) override;
  bool Notify(camera3_notify_msg_t* msg) override;
  bool Flush() override;

 private:
  // Used to fill in JPEG buffer with a black JPEG image. Returns true if
  // successful. Returns false otherwise.
  bool FillInFrameWithBlackJpegImage(buffer_handle_t handle,
                                     ScopedMapping& mapping,
                                     int width,
                                     int height);

  // Contains the current software privacy switch state.
  RuntimeOptions* runtime_options_;

  // CameraBufferManager instance.
  CameraBufferManager* camera_buffer_manager_;

  // JPEG compressor instance.
  std::unique_ptr<JpegCompressor> jpeg_compressor_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_SW_PRIVACY_SWITCH_STREAM_MANIPULATOR_H_
