// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/fake_diagnostics_service.h"

#include <utility>

#include "diagnostics/common/mojo_utils.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

FakeDiagnosticsService::FakeDiagnosticsService() = default;
FakeDiagnosticsService::~FakeDiagnosticsService() = default;

bool FakeDiagnosticsService::GetCrosHealthdDiagnosticsService(
    mojo_ipc::CrosHealthdDiagnosticsServiceRequest service) {
  if (!is_available_)
    return false;

  service_binding_.Bind(std::move(service));
  return true;
}

void FakeDiagnosticsService::GetAvailableRoutines(
    const GetAvailableRoutinesCallback& callback) {
  callback.Run(available_routines_);
}

void FakeDiagnosticsService::GetRoutineUpdate(
    int32_t id,
    mojo_ipc::DiagnosticRoutineCommandEnum command,
    bool include_output,
    const GetRoutineUpdateCallback& callback) {
  callback.Run(mojo_ipc::RoutineUpdate::New(
      routine_update_response_.progress_percent,
      std::move(routine_update_response_.output),
      std::move(routine_update_response_.routine_update_union)));
}

void FakeDiagnosticsService::RunUrandomRoutine(
    uint32_t length_seconds, const RunUrandomRoutineCallback& callback) {
  callback.Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunBatteryCapacityRoutine(
    uint32_t low_mah,
    uint32_t high_mah,
    const RunBatteryCapacityRoutineCallback& callback) {
  callback.Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed,
    const RunBatteryHealthRoutineCallback& callback) {
  callback.Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunSmartctlCheckRoutine(
    const RunSmartctlCheckRoutineCallback& callback) {
  callback.Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::SetMojoServiceNotAvailableResponse() {
  is_available_ = false;
}

void FakeDiagnosticsService::SetGetAvailableRoutinesResponse(
    const std::vector<mojo_ipc::DiagnosticRoutineEnum>& available_routines) {
  available_routines_ = available_routines;
}

void FakeDiagnosticsService::SetInteractiveUpdate(
    mojo_ipc::DiagnosticRoutineUserMessageEnum user_message,
    uint32_t progress_percent,
    const std::string& output) {
  routine_update_response_.progress_percent = progress_percent;
  routine_update_response_.output =
      CreateReadOnlySharedMemoryMojoHandle(output);
  mojo_ipc::InteractiveRoutineUpdate interactive_update;
  interactive_update.user_message = user_message;
  routine_update_response_.routine_update_union->set_interactive_update(
      interactive_update.Clone());
}

void FakeDiagnosticsService::SetNonInteractiveUpdate(
    mojo_ipc::DiagnosticRoutineStatusEnum status,
    const std::string& status_message,
    uint32_t progress_percent,
    const std::string& output) {
  routine_update_response_.progress_percent = progress_percent;
  routine_update_response_.output =
      CreateReadOnlySharedMemoryMojoHandle(output);
  mojo_ipc::NonInteractiveRoutineUpdate noninteractive_update;
  noninteractive_update.status = status;
  noninteractive_update.status_message = status_message;
  routine_update_response_.routine_update_union->set_noninteractive_update(
      noninteractive_update.Clone());
}

void FakeDiagnosticsService::SetRunSomeRoutineResponse(
    uint32_t id, mojo_ipc::DiagnosticRoutineStatusEnum status) {
  run_routine_response_.id = id;
  run_routine_response_.status = status;
}

}  // namespace diagnostics
