// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_FRAME_ANALYSIS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_FRAME_ANALYSIS_H_

#include <base/memory/weak_ptr.h>
#include <camera/mojo/camera_diagnostics.mojom-forward.h>

#include "diagnostics/cros_healthd/routines/noninteractive_routine_control.h"

namespace diagnostics {
class Context;

class CameraFrameAnalysisRoutine final : public NoninteractiveRoutineControl {
 public:
  explicit CameraFrameAnalysisRoutine(Context* context);
  CameraFrameAnalysisRoutine(const CameraFrameAnalysisRoutine&) = delete;
  CameraFrameAnalysisRoutine& operator=(const CameraFrameAnalysisRoutine&) =
      delete;
  ~CameraFrameAnalysisRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  void OnResult(::cros::camera_diag::mojom::FrameAnalysisResultPtr result);
  void OnErrorResult(::cros::camera_diag::mojom::ErrorCode error_code);
  void OnSuccessResult(
      const ::cros::camera_diag::mojom::DiagnosticsResultPtr& result);

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
  // Must be the last class member.
  base::WeakPtrFactory<CameraFrameAnalysisRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_FRAME_ANALYSIS_H_
