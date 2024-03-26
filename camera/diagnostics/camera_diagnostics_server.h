// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_SERVER_H_
#define CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_SERVER_H_

#include "camera/common/utils/camera_mojo_service_provider.h"
#include "camera/mojo/camera_diagnostics.mojom.h"
#include "diagnostics/camera_diagnostics_mojo_manager.h"
#include "diagnostics/camera_diagnostics_processor.h"

namespace cros {

// Not thread-safe; needs to be created and destroyed in the IPC thread.
class CameraDiagnosticsServer final
    : public camera_diag::mojom::CameraDiagnostics,
      public camera_diag::mojom::CrosCameraDiagnosticsService {
 public:
  explicit CameraDiagnosticsServer(CameraDiagnosticsMojoManager* mojo_manager);
  CameraDiagnosticsServer(CameraDiagnosticsServer&) = delete;
  CameraDiagnosticsServer& operator=(const CameraDiagnosticsServer&) = delete;

  // Starts frame analysis.
  // Returns error if analysis already running or failure.
  // Params: Callbacks for result.
  void RunFrameAnalysis(camera_diag::mojom::FrameAnalysisConfigPtr config,
                        RunFrameAnalysisCallback callback) final;

  void SendFrame(camera_diag::mojom::CameraFramePtr frame) final;

 private:
  CameraDiagnosticsMojoManager* mojo_manager_;

  CameraDiagnosticsProcessor processor_;

  internal::CameraMojoServiceProvider<camera_diag::mojom::CameraDiagnostics>
      diag_provider_{this};
  internal::CameraMojoServiceProvider<
      camera_diag::mojom::CrosCameraDiagnosticsService>
      diag_service_provider_{this};
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_SERVER_H_
