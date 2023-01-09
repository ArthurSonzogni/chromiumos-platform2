// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/https_firewall.h"

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
    network_diagnostics_ipc::HttpsFirewallProblem problem) {
  switch (problem) {
    case network_diagnostics_ipc::HttpsFirewallProblem::
        kHighDnsResolutionFailureRate:
      return kHttpsFirewallRoutineHighDnsResolutionFailureRateProblemMessage;
    case network_diagnostics_ipc::HttpsFirewallProblem::kFirewallDetected:
      return kHttpsFirewallRoutineFirewallDetectedProblemMessage;
    case network_diagnostics_ipc::HttpsFirewallProblem::kPotentialFirewall:
      return kHttpsFirewallRoutinePotentialFirewallProblemMessage;
  }
}

// Parses the results of the HTTPS firewall routine.
void ParseHttpsFirewallResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineResultPtr result) {
  DCHECK(status);
  DCHECK(status_message);

  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kHttpsFirewallRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kHttpsFirewallRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      auto problems = result->problems->get_https_firewall_problems();
      DCHECK(!problems.empty());
      *status_message = GetProblemMessage(problems[0]);
      break;
  }
}

// We include |output| here to satisfy SimpleRoutine - the HTTPS firewall
// routine never includes an output.
void RunHttpsFirewallRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunHttpsFirewallRoutine(
      base::BindOnce(&ParseHttpsFirewallResult, status, status_message));
}

}  // namespace

const char kHttpsFirewallRoutineNoProblemMessage[] =
    "HTTPS firewall routine passed with no problems.";
const char kHttpsFirewallRoutineHighDnsResolutionFailureRateProblemMessage[] =
    "DNS resolution failure rate is high.";
const char kHttpsFirewallRoutineFirewallDetectedProblemMessage[] =
    "Firewall detected.";
const char kHttpsFirewallRoutinePotentialFirewallProblemMessage[] =
    "A firewall may potentially exist.";
const char kHttpsFirewallRoutineNotRunMessage[] =
    "HTTPS firewall routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateHttpsFirewallRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunHttpsFirewallRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
