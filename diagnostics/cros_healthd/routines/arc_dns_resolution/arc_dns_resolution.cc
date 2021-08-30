// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/arc_dns_resolution/arc_dns_resolution.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/values.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

// Parses the results of the ARC DNS resolution routine.
void ParseArcDnsResolutionResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineResultPtr result) {
  DCHECK(status);
  DCHECK(status_message);

  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kArcDnsResolutionRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kArcDnsResolutionRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      auto problems = result->problems->get_arc_dns_resolution_problems();
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::ArcDnsResolutionProblem::
            kFailedToGetArcServiceManager:
          *status_message =
              kArcDnsResolutionRoutineFailedToGetArcServiceManagerMessage;
          break;
        case network_diagnostics_ipc::ArcDnsResolutionProblem::
            kFailedToGetNetInstanceForDnsResolutionTest:
          *status_message =
              kArcDnsResolutionRoutineFailedToGetNetInstanceMessage;
          break;
        case network_diagnostics_ipc::ArcDnsResolutionProblem::kHighLatency:
          *status_message = kArcDnsResolutionRoutineHighLatencyMessage;
          break;
        case network_diagnostics_ipc::ArcDnsResolutionProblem::kVeryHighLatency:
          *status_message = kArcDnsResolutionRoutineVeryHighLatencyMessage;
          break;
        case network_diagnostics_ipc::ArcDnsResolutionProblem::
            kFailedDnsQueries:
          *status_message = kArcDnsResolutionRoutineFailedDnsQueriesMessage;
          break;
      }
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the DNS resolution
// routine never includes an output.
void RunArcDnsResolutionRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunArcDnsResolutionRoutine(
      base::BindOnce(&ParseArcDnsResolutionResult, status, status_message));
}

}  // namespace

std::unique_ptr<DiagnosticRoutine> CreateArcDnsResolutionRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunArcDnsResolutionRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
