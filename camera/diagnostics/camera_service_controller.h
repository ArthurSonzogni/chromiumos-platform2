// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_CAMERA_SERVICE_CONTROLLER_H_
#define CAMERA_DIAGNOSTICS_CAMERA_SERVICE_CONTROLLER_H_

#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "diagnostics/camera_diagnostics_helpers.h"
#include "diagnostics/camera_diagnostics_mojo_manager.h"

namespace cros {

// Provides safe access to |camera_diag::mojom::CrosCameraController|.
// Thread-safe.
class CameraServiceController {
 public:
  explicit CameraServiceController(CameraDiagnosticsMojoManager* mojo_manager);
  CameraServiceController(const CameraServiceController&) = delete;
  CameraServiceController& operator=(const CameraServiceController&) = delete;

  ~CameraServiceController();

  void StartStreaming(camera_diag::mojom::StreamingConfigPtr config,
                      CameraStartStreamingCallback callback);

  void StopStreaming();

  void RequestFrame(camera_diag::mojom::CameraFramePtr frame);

 private:
  //
  // All the following functions need to be run on |ipc_task_runner_|.
  //

  void InitiateStartStreaming(camera_diag::mojom::StreamingConfigPtr config,
                              CameraStartStreamingCallback callback);

  void StartStreamingInternal(
      camera_diag::mojom::StreamingConfigPtr config,
      CameraStartStreamingCallback callback,
      chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr
          err_or_state);

  void StopStreamingInternal();

  void RequestFrameInternal(camera_diag::mojom::CameraFramePtr frame);

  void ResetRemote(base::OnceClosure callback);

  CameraDiagnosticsMojoManager* mojo_manager_;

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;

  mojo::Remote<camera_diag::mojom::CrosCameraController> remote_;
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_CAMERA_SERVICE_CONTROLLER_H_
