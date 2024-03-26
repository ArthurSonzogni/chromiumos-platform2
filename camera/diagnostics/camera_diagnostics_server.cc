// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera/diagnostics/camera_diagnostics_server.h"

#include <utility>

#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/task/bind_post_task.h>
#include <chromeos/mojo/service_constants.h>

#include "diagnostics/camera_diagnostics_processor.h"

namespace cros {

CameraDiagnosticsServer::CameraDiagnosticsServer(
    CameraDiagnosticsMojoManager* mojo_manager)
    : mojo_manager_(mojo_manager) {
  diag_provider_.Register(mojo_manager->GetMojoServiceManager().get(),
                          chromeos::mojo_services::kCrosCameraDiagnostics);
  diag_service_provider_.Register(
      mojo_manager->GetMojoServiceManager().get(),
      chromeos::mojo_services::kCrosCameraDiagnosticsService);
}

void CameraDiagnosticsServer::RunFrameAnalysis(
    camera_diag::mojom::FrameAnalysisConfigPtr config,
    RunFrameAnalysisCallback callback) {
  auto result_callback =
      base::BindPostTask(mojo_manager_->GetTaskRunner(), std::move(callback));
  processor_.RunFrameAnalysis(std::move(config), std::move(result_callback));
}

void CameraDiagnosticsServer::SendFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  processor_.QueueFrame(std::move(frame));
}

}  // namespace cros
