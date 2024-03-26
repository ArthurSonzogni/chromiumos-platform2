// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_PROCESSOR_H_
#define CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_PROCESSOR_H_

#include <memory>

#include <base/thread_annotations.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/camera_thread.h"
#include "diagnostics/camera_diagnostics_session.h"

namespace cros {

// Main processor of camera diagnostics. The camera diagnositcs
// server forwards all the requests and data to this.
// This processor creates a new session when the client starts a
// diagnosis. Only one session runs at a time.
// Thread-safe.
class CameraDiagnosticsProcessor {
 public:
  using RunFrameAnalysisCallback =
      camera_diag::mojom::CameraDiagnostics::RunFrameAnalysisCallback;

  CameraDiagnosticsProcessor();
  CameraDiagnosticsProcessor(const CameraDiagnosticsProcessor&) = delete;
  CameraDiagnosticsProcessor& operator=(const CameraDiagnosticsProcessor&) =
      delete;
  CameraDiagnosticsProcessor(CameraDiagnosticsProcessor&&) = delete;
  CameraDiagnosticsProcessor& operator=(CameraDiagnosticsProcessor&&) = delete;

  ~CameraDiagnosticsProcessor() = default;

  void QueueFrame(camera_diag::mojom::CameraFramePtr frame);

  void RunFrameAnalysis(camera_diag::mojom::FrameAnalysisConfigPtr config,
                        RunFrameAnalysisCallback callback);

 private:
  // Creates a new session and starts frame analysis. Blocks the thread
  // until frame analysis is finished.
  void RunFrameAnalysisOnThread(
      camera_diag::mojom::FrameAnalysisConfigPtr config,
      RunFrameAnalysisCallback callback);

  void ReturnErrorResult(RunFrameAnalysisCallback callback,
                         camera_diag::mojom::ErrorCode error);

  void ResetSession();

  CameraThread thread_;

  base::Lock session_lock_;
  std::unique_ptr<CameraDiagnosticsSession> current_session_
      GUARDED_BY(session_lock_);
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_PROCESSOR_H_
