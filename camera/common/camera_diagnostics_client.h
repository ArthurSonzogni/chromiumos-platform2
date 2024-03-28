// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_COMMON_CAMERA_DIAGNOSTICS_CLIENT_H_
#define CAMERA_COMMON_CAMERA_DIAGNOSTICS_CLIENT_H_

#include <optional>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common_types.h"
#include "cros-camera/export.h"

namespace cros {

class CROS_CAMERA_EXPORT CameraDiagnosticsClient {
 public:
  virtual ~CameraDiagnosticsClient() = default;

  virtual bool IsFrameAnalysisEnabled() = 0;

  virtual uint32_t frame_interval() const = 0;

  // Request an empty frame to fill in.
  virtual std::optional<camera_diag::mojom::CameraFramePtr>
  RequestEmptyFrame() = 0;

  // Sends a frame to camera diagnostics service.
  virtual void SendFrame(camera_diag::mojom::CameraFramePtr frame) = 0;

  // Currently, only supports one open camera at a time.
  // No-op when a session is already in progress.
  virtual void AddCameraSession(const Size& stream_size) = 0;

  // Removes the current session.
  virtual void RemoveCameraSession() = 0;
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_DIAGNOSTICS_CLIENT_H_
