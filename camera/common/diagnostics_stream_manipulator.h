// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_COMMON_DIAGNOSTICS_STREAM_MANIPULATOR_H_
#define CAMERA_COMMON_DIAGNOSTICS_STREAM_MANIPULATOR_H_

#include <cutils/native_handle.h>
#include <drm_fourcc.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "common/camera_diagnostics_client.h"
#include "common/camera_hal3_helpers.h"
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
  void Reset();
  bool FillDiagnosticsBuffer(
      const Size& target_size,
      Camera3StreamBuffer& stream_buffer,
      camera_diag::mojom::CameraFrameBufferPtr& out_frame);
  bool ValidateDiagnosticsFrame(
      const camera_diag::mojom::CameraFramePtr& frame);

  CameraBufferManager* camera_buffer_manager_;
  StreamManipulator::Callbacks callbacks_;
  CameraDiagnosticsClient* diagnostics_client_;

  const camera3_stream_t* selected_stream_ = nullptr;
  int next_target_frame_number_ = 0;
};

}  // namespace cros

#endif  // CAMERA_COMMON_DIAGNOSTICS_STREAM_MANIPULATOR_H_
