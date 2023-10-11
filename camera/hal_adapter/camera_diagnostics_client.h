// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_H_
#define CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_H_

#include <base/task/single_thread_task_runner.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "common/camera_diagnostics_config.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "hal_adapter/camera_hal_adapter.h"

namespace cros {

// A wrapper for mojo call to camera diagnostics service.
class CameraDiagnosticsClient {
 public:
  explicit CameraDiagnosticsClient(CameraMojoChannelManager* mojo_manager_,
                                   CameraHalAdapter* camera_hal_adapter);
  CameraDiagnosticsClient(const CameraDiagnosticsClient&) = delete;
  CameraDiagnosticsClient& operator=(const CameraDiagnosticsClient&) = delete;
  ~CameraDiagnosticsClient() = default;

 private:
  // Used to dispatch a frame to camera diagnostics service.
  void AnalyzeYuvFrame(mojom::CameraDiagnosticsFramePtr buffer);
  void ResetRemotePtr();
  // Binds the mojo remote.
  void Bind();
  void OnDisconnect();
  void OnAnalyzedFrameReply(mojom::Response res);

  CameraMojoChannelManager* mojo_manager_;

  const scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner_;

  mojo::Remote<cros::mojom::CameraDiagnostics> remote_;

  CameraDiagnosticsConfig camera_diagnostics_config_;

  CameraHalAdapter* camera_hal_adapter_;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_H_
