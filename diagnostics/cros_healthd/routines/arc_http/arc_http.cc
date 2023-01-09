// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/arc_http/arc_http.h"

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

std::string GetProblemMessage(network_diagnostics_ipc::ArcHttpProblem problem) {
  switch (problem) {
    case network_diagnostics_ipc::ArcHttpProblem::kFailedToGetArcServiceManager:
      return kArcHttpRoutineFailedToGetArcServiceManagerMessage;
    case network_diagnostics_ipc::ArcHttpProblem::
        kFailedToGetNetInstanceForHttpTest:
      return kArcHttpRoutineFailedToGetNetInstanceForHttpTestMessage;
    case network_diagnostics_ipc::ArcHttpProblem::kFailedHttpRequests:
      return kArcHttpRoutineFailedHttpsRequestsProblemMessage;
    case network_diagnostics_ipc::ArcHttpProblem::kHighLatency:
      return kArcHttpRoutineHighLatencyProblemMessage;
    case network_diagnostics_ipc::ArcHttpProblem::kVeryHighLatency:
      return kArcHttpRoutineVeryHighLatencyProblemMessage;
  }
}

// Parses the results of the ARC HTTP routine.
void ParseArcHttpResult(mojo_ipc::DiagnosticRoutineStatusEnum* status,
                        std::string* status_message,
                        network_diagnostics_ipc::RoutineResultPtr result) {
  DCHECK(status);
  DCHECK(status_message);

  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kArcHttpRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kArcHttpRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      auto problems = result->problems->get_arc_http_problems();
      DCHECK(!problems.empty());
      *status_message = GetProblemMessage(problems[0]);
      break;
  }
}

// We include |output| here to satisfy SimpleRoutine - the ARC HTTP
// routine never includes an output.
void RunArcHttpRoutine(NetworkDiagnosticsAdapter* network_diagnostics_adapter,
                       mojo_ipc::DiagnosticRoutineStatusEnum* status,
                       std::string* status_message,
                       base::Value* output) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunArcHttpRoutine(
      base::BindOnce(&ParseArcHttpResult, status, status_message));
}

}  // namespace

std::unique_ptr<DiagnosticRoutine> CreateArcHttpRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunArcHttpRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
