// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_FRAME_ANALYSIS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_FRAME_ANALYSIS_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <camera/mojo/camera_diagnostics.mojom-forward.h>

#include "diagnostics/cros_healthd/routines/noninteractive_routine_control.h"

namespace base {
class ElapsedTimer;
}  // namespace base

namespace diagnostics {
class Context;

class CameraFrameAnalysisRoutine final : public NoninteractiveRoutineControl {
 public:
  // Duration for the frame analysis.
  static constexpr uint32_t kExecutionDurationMilliseconds = 5000;

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
  void OnCallbackDropped();
  // Update the progress percentage of the routine.
  void UpdatePercentage();

  // The duration of the frame analysis. The value is initialized in
  // `OnStart()`.
  base::TimeDelta execution_duration_;

  // A timer for progress percentage calculation. The object is initialized in
  // `OnStart()`.
  std::unique_ptr<base::ElapsedTimer> elapsed_timer_;

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
  // Must be the last class member.
  base::WeakPtrFactory<CameraFrameAnalysisRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_FRAME_ANALYSIS_H_
