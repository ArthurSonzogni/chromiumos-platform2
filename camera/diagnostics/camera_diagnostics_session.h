// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_SESSION_H_
#define CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_SESSION_H_

#include <base/memory/scoped_refptr.h>
#include <base/threading/thread_checker.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/camera_thread.h"
#include "diagnostics/camera_diagnostics_mojo_manager.h"
#include "diagnostics/camera_service_controller.h"

namespace cros {

// Each session is responsible to run one full diagnosis.
// To free resources, sessions should be destroyed when diagnosis
// is finished and result is retrieved.
// Thread-safe.
class CameraDiagnosticsSession {
 public:
  CameraDiagnosticsSession(CameraDiagnosticsMojoManager* mojo_manager,
                           scoped_refptr<Future<void>> notify_finish);
  CameraDiagnosticsSession(const CameraDiagnosticsSession&) = delete;
  CameraDiagnosticsSession& operator=(const CameraDiagnosticsSession&) = delete;
  CameraDiagnosticsSession(CameraDiagnosticsSession&&) = delete;
  CameraDiagnosticsSession& operator=(CameraDiagnosticsSession&&) = delete;

  ~CameraDiagnosticsSession() = default;

  void QueueFrame(camera_diag::mojom::CameraFramePtr frame);

  // When frame analysis starts, this calls camera service to start streaming.
  // Triggers |notify_finish_| when diagnosis finishes.
  void RunFrameAnalysis(camera_diag::mojom::FrameAnalysisConfigPtr config);

  camera_diag::mojom::FrameAnalysisResultPtr StopAndGetResult();

 private:
  void RunFrameAnalysisOnThread(
      camera_diag::mojom::FrameAnalysisConfigPtr config);

  void OnStartStreaming(camera_diag::mojom::StartStreamingResultPtr result);

  void PrepareResult();

  CameraThread thread_;

  CameraServiceController camera_service_controller_;

  base::Lock lock_;
  std::optional<camera_diag::mojom::FrameAnalysisResultPtr> result_
      GUARDED_BY(lock_) = std::nullopt;

  scoped_refptr<Future<void>> notify_finish_;
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_SESSION_H_
