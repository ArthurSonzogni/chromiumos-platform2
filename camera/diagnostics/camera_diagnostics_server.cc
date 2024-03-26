// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera/diagnostics/camera_diagnostics_server.h"

#include <base/functional/callback_helpers.h>
#include <chromeos/mojo/service_constants.h>

namespace cros {

CameraDiagnosticsServer::CameraDiagnosticsServer(
    CameraDiagnosticsMojoManager* mojo_manager) {
  diag_provider_.Register(mojo_manager->GetMojoServiceManager().get(),
                          chromeos::mojo_services::kCrosCameraDiagnostics);
  diag_service_provider_.Register(
      mojo_manager->GetMojoServiceManager().get(),
      chromeos::mojo_services::kCrosCameraDiagnosticsService);
}

void CameraDiagnosticsServer::RunFrameAnalysis(
    camera_diag::mojom::FrameAnalysisConfigPtr config,
    RunFrameAnalysisCallback callback) {
  // TODO(imranziad): Start analysis on processor.
}

void CameraDiagnosticsServer::SendFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  // TODO(imranziad): Send to processor.
}

}  // namespace cros
