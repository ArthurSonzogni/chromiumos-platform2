// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/gateway_can_be_pinged.h"

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

namespace mojo_ipc = ::ash::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

std::string GetProblemMessage(
    network_diagnostics_ipc::GatewayCanBePingedProblem problem) {
  switch (problem) {
    case network_diagnostics_ipc::GatewayCanBePingedProblem::
        kUnreachableGateway:
      return kPingRoutineUnreachableGatewayProblemMessage;
    case network_diagnostics_ipc::GatewayCanBePingedProblem::
        kFailedToPingDefaultNetwork:
      return kPingRoutineFailedPingProblemMessage;
    case network_diagnostics_ipc::GatewayCanBePingedProblem::
        kDefaultNetworkAboveLatencyThreshold:
      return kPingRoutineHighPingLatencyProblemMessage;
    case network_diagnostics_ipc::GatewayCanBePingedProblem::
        kUnsuccessfulNonDefaultNetworksPings:
      return kPingRoutineFailedNonDefaultPingsProblemMessage;
    case network_diagnostics_ipc::GatewayCanBePingedProblem::
        kNonDefaultNetworksAboveLatencyThreshold:
      return kPingRoutineNonDefaultHighLatencyProblemMessage;
  }
}

// Parses the results of the gateway can be pinged routine.
void ParseGatewayCanBePingedResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineResultPtr result) {
  DCHECK(status);
  DCHECK(status_message);

  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kPingRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kPingRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      auto problems = result->problems->get_gateway_can_be_pinged_problems();
      DCHECK(!problems.empty());
      *status_message = GetProblemMessage(problems[0]);
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the gateway can be
// pinged routine never includes an output.
void RunGatewayCanBePingedRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunGatewayCanBePingedRoutine(
      base::BindOnce(&ParseGatewayCanBePingedResult, status, status_message));
}

}  // namespace

const char kPingRoutineNoProblemMessage[] =
    "Gateway can be pinged routine passed with no problems.";
const char kPingRoutineUnreachableGatewayProblemMessage[] =
    "All gateways are unreachable, hence cannot be pinged.";
const char kPingRoutineFailedPingProblemMessage[] =
    "The default network cannot be pinged.";
const char kPingRoutineHighPingLatencyProblemMessage[] =
    "The default network has a latency above the threshold.";
const char kPingRoutineFailedNonDefaultPingsProblemMessage[] =
    "One or more of the non-default networks has failed pings.";
const char kPingRoutineNonDefaultHighLatencyProblemMessage[] =
    "One or more of the non-default networks has a latency above the "
    "threshold.";
const char kPingRoutineNotRunMessage[] =
    "Gateway can be pinged routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateGatewayCanBePingedRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(base::BindOnce(
      &RunGatewayCanBePingedRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
