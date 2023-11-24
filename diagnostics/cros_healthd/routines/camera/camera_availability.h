// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_AVAILABILITY_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_AVAILABILITY_H_

#include <optional>
#include <string>

#include <base/memory/weak_ptr.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
class Context;

// The camera availability routine checks the availability of services related
// to cameras.
class CameraAvailabilityRoutine final : public BaseRoutineControl {
 public:
  explicit CameraAvailabilityRoutine(
      Context* context,
      const ash::cros_healthd::mojom::CameraAvailabilityRoutineArgumentPtr&
          arg);
  CameraAvailabilityRoutine(const CameraAvailabilityRoutine&) = delete;
  CameraAvailabilityRoutine& operator=(const CameraAvailabilityRoutine&) =
      delete;
  ~CameraAvailabilityRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  void HandleQueryCameraServiceState(
      chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr
          error_or_service_state);
  void HandleQueryCameraDiagnosticServiceState(
      chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr
          error_or_service_state);
  void OnAllSubtestsFinished(bool all_callbacks_invoked);

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
  // Whether to check the availability of the camera service.
  const bool run_camera_service_available_check = false;
  // Whether to check the availability of the camera diagnostic service.
  const bool run_camera_diagnostic_service_available_check = false;
  // The message for errors occurred in subtests. Only the last error message
  // will be reported.
  std::optional<std::string> error_message_;
  // The detail of the result.
  ash::cros_healthd::mojom::CameraAvailabilityRoutineDetailPtr routine_detail;
  // Must be the last class member.
  base::WeakPtrFactory<CameraAvailabilityRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CAMERA_CAMERA_AVAILABILITY_H_
