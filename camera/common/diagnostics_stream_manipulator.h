// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_COMMON_DIAGNOSTICS_STREAM_MANIPULATOR_H_
#define CAMERA_COMMON_DIAGNOSTICS_STREAM_MANIPULATOR_H_

#include <cutils/native_handle.h>
#include <drm_fourcc.h>

#include "common/camera_diagnostics_client.h"
#include "common/stream_manipulator.h"

namespace cros {

class DiagnosticsStreamManipulator : public StreamManipulator {
 public:
  explicit DiagnosticsStreamManipulator(
      CameraDiagnosticsClient* diagnostics_client);

  ~DiagnosticsStreamManipulator() override;

  // Implementations of StreamManipulator.
  bool Initialize(const camera_metadata_t* static_info,
                  StreamManipulator::Callbacks callbacks) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override;
  void Notify(camera3_notify_msg_t msg) override;
  bool Flush() override;

 private:
  CameraBufferManager* camera_buffer_manager_;
  StreamManipulator::Callbacks callbacks_;
  CameraDiagnosticsClient* diagnostics_client_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_DIAGNOSTICS_STREAM_MANIPULATOR_H_
