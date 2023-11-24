// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/camera/camera_availability.h"

#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/types/expected.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace service_manager_mojom = ::chromeos::mojo_service_manager::mojom;

base::expected<bool, std::string> IsMojoServiceAvailable(
    const service_manager_mojom::ErrorOrServiceStatePtr&
        error_or_service_state) {
  if (!error_or_service_state) {
    return base::unexpected("Response of mojo service state is null.");
  }
  if (error_or_service_state->is_state()) {
    const auto& state = error_or_service_state->get_state();
    return base::ok(state->is_registered_state());
  }
  return base::unexpected("Error in mojo service state.");
}

}  // namespace

CameraAvailabilityRoutine::CameraAvailabilityRoutine(
    Context* context, const mojom::CameraAvailabilityRoutineArgumentPtr& arg)
    : context_(context),
      run_camera_service_available_check(
          arg->run_camera_service_available_check),
      run_camera_diagnostic_service_available_check(
          arg->run_camera_diagnostic_service_available_check) {
  CHECK(context_);
  routine_detail = mojom::CameraAvailabilityRoutineDetail::New();
}

CameraAvailabilityRoutine::~CameraAvailabilityRoutine() = default;

void CameraAvailabilityRoutine::OnStart() {
  SetRunningState();

  auto* mojo_service = context_->mojo_service();
  CHECK(mojo_service);

  auto* mojo_service_manager = mojo_service->GetServiceManager();
  if (!mojo_service_manager) {
    RaiseException("Failed to access mojo service manager.");
    return;
  }

  CallbackBarrier barrier{
      base::BindOnce(&CameraAvailabilityRoutine::OnAllSubtestsFinished,
                     weak_ptr_factory_.GetWeakPtr())};

  if (!run_camera_service_available_check) {
    routine_detail->camera_service_available_check =
        mojom::CameraSubtestResult::kNotRun;
  } else {
    mojo_service_manager->Query(
        chromeos::mojo_services::kCrosCameraService,
        barrier.Depend(base::BindOnce(
            &CameraAvailabilityRoutine::HandleQueryCameraServiceState,
            weak_ptr_factory_.GetWeakPtr())));
  }

  if (!run_camera_diagnostic_service_available_check) {
    routine_detail->camera_diagnostic_service_available_check =
        mojom::CameraSubtestResult::kNotRun;
  } else {
    mojo_service_manager->Query(
        chromeos::mojo_services::kCrosCameraDiagnostics,
        barrier.Depend(base::BindOnce(
            &CameraAvailabilityRoutine::HandleQueryCameraDiagnosticServiceState,
            weak_ptr_factory_.GetWeakPtr())));
  }
}

void CameraAvailabilityRoutine::HandleQueryCameraServiceState(
    service_manager_mojom::ErrorOrServiceStatePtr error_or_service_state) {
  const auto& result = IsMojoServiceAvailable(error_or_service_state);
  if (result.has_value()) {
    routine_detail->camera_service_available_check =
        result.value() ? mojom::CameraSubtestResult::kPassed
                       : mojom::CameraSubtestResult::kFailed;
  } else {
    LOG(ERROR) << "Error in HandleQueryCameraServiceState: " << result.error();
    error_message_ = result.error();
  }
}

void CameraAvailabilityRoutine::HandleQueryCameraDiagnosticServiceState(
    service_manager_mojom::ErrorOrServiceStatePtr error_or_service_state) {
  const auto& result = IsMojoServiceAvailable(error_or_service_state);
  if (result.has_value()) {
    routine_detail->camera_diagnostic_service_available_check =
        result.value() ? mojom::CameraSubtestResult::kPassed
                       : mojom::CameraSubtestResult::kFailed;
  } else {
    LOG(ERROR) << "Error in HandleQueryCameraDiagnosticServiceState: "
               << result.error();
    error_message_ = result.error();
  }
}

void CameraAvailabilityRoutine::OnAllSubtestsFinished(
    bool all_callbacks_invoked) {
  if (!all_callbacks_invoked) {
    RaiseException("Some callbacks are dropped.");
    return;
  }

  if (error_message_.has_value()) {
    RaiseException(error_message_.value());
    return;
  }

  // The result of camera diagnostic service subtest is informational. It does
  // not affect the routine's passed state.
  bool has_passed = (routine_detail->camera_service_available_check !=
                     mojom::CameraSubtestResult::kFailed);
  SetFinishedState(has_passed, mojom::RoutineDetail::NewCameraAvailability(
                                   std::move(routine_detail)));
}

}  // namespace diagnostics
