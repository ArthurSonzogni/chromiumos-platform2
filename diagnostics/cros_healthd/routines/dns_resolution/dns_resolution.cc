// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/dns_resolution/dns_resolution.h"

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

// Parses the results of the DNS resolution routine.
void ParseDnsResolutionResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineResultPtr result) {
  DCHECK(status);
  DCHECK(status_message);

  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kDnsResolutionRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kDnsResolutionRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      auto problems = result->problems->get_dns_resolution_problems();
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::DnsResolutionProblem::
            kFailedToResolveHost:
          *status_message =
              kDnsResolutionRoutineFailedToResolveHostProblemMessage;
          break;
      }
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the DNS resolution
// routine never includes an output.
void RunDnsResolutionRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunDnsResolutionRoutine(
      base::BindOnce(&ParseDnsResolutionResult, status, status_message));
}

}  // namespace

const char kDnsResolutionRoutineNoProblemMessage[] =
    "DNS resolution routine passed with no problems.";
const char kDnsResolutionRoutineFailedToResolveHostProblemMessage[] =
    "Failed to resolve host.";
const char kDnsResolutionRoutineNotRunMessage[] =
    "DNS resolution routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateDnsResolutionRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunDnsResolutionRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
