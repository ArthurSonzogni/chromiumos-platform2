// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/arc_ping/arc_ping.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/values.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

// Parses the results of ARC ping routine.
void ParseArcPingResult(mojo_ipc::DiagnosticRoutineStatusEnum* status,
                        std::string* status_message,
                        network_diagnostics_ipc::RoutineResultPtr result) {
  DCHECK(status);
  DCHECK(status_message);

  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kArcPingRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kArcPingRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      auto problems = result->problems->get_arc_ping_problems();
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::ArcPingProblem::
            kFailedToGetArcServiceManager:
          *status_message = kArcPingRoutineFailedToGetArcServiceManagerMessage;
          break;
        case network_diagnostics_ipc::ArcPingProblem::
            kFailedToGetNetInstanceForPingTest:
          *status_message =
              kArcPingRoutineFailedToGetNetInstanceForPingTestMessage;
          break;
        case network_diagnostics_ipc::ArcPingProblem::
            kGetManagedPropertiesTimeoutFailure:
          *status_message =
              kArcPingRoutineGetManagedPropertiesTimeoutFailureMessage;
          break;
        case network_diagnostics_ipc::ArcPingProblem::kUnreachableGateway:
          *status_message = kArcPingRoutineUnreachableGatewayMessage;
          break;
        case network_diagnostics_ipc::ArcPingProblem::
            kFailedToPingDefaultNetwork:
          *status_message = kArcPingRoutineFailedToPingDefaultNetworkMessage;
          break;
        case network_diagnostics_ipc::ArcPingProblem::
            kDefaultNetworkAboveLatencyThreshold:
          *status_message =
              kArcPingRoutineDefaultNetworkAboveLatencyThresholdMessage;
          break;
        case network_diagnostics_ipc::ArcPingProblem::
            kUnsuccessfulNonDefaultNetworksPings:
          *status_message =
              kArcPingRoutineUnsuccessfulNonDefaultNetworksPingsMessage;
          break;
        case network_diagnostics_ipc::ArcPingProblem::
            kNonDefaultNetworksAboveLatencyThreshold:
          *status_message =
              kArcPingRoutineNonDefaultNetworksAboveLatencyThresholdMessage;
          break;
      }
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the gateway can be
// pinged routine never includes an output.
void RunArcPingRoutine(NetworkDiagnosticsAdapter* network_diagnostics_adapter,
                       mojo_ipc::DiagnosticRoutineStatusEnum* status,
                       std::string* status_message,
                       base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunArcPingRoutine(
      base::BindOnce(&ParseArcPingResult, status, status_message));
}

}  // namespace

std::unique_ptr<DiagnosticRoutine> CreateArcPingRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunArcPingRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
