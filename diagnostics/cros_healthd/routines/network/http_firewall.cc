// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/http_firewall.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

void PrintCallbackDropped() {
  LOG(ERROR) << "RunHttpFirewall callback dropped";
}

std::string GetProblemMessage(
    network_diagnostics_ipc::HttpFirewallProblem problem) {
  switch (problem) {
    case network_diagnostics_ipc::HttpFirewallProblem::
        kDnsResolutionFailuresAboveThreshold:
      return kHttpFirewallRoutineHighDnsResolutionFailureRateProblemMessage;
    case network_diagnostics_ipc::HttpFirewallProblem::kFirewallDetected:
      return kHttpFirewallRoutineFirewallDetectedProblemMessage;
    case network_diagnostics_ipc::HttpFirewallProblem::kPotentialFirewall:
      return kHttpFirewallRoutinePotentialFirewallProblemMessage;
  }
}

// Parses the results of the HTTP firewall routine.
SimpleRoutine::RoutineResult ParseHttpFirewallResult(
    network_diagnostics_ipc::RoutineResultPtr result) {
  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kPassed,
          .status_message = kHttpFirewallRoutineNoProblemMessage,
      };
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kNotRun,
          .status_message = kHttpFirewallRoutineNotRunMessage,
      };
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      auto problems = result->problems->get_http_firewall_problems();
      DCHECK(!problems.empty());
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kFailed,
          .status_message = GetProblemMessage(problems[0]),
      };
  }
}

void RunHttpFirewallRoutine(MojoService* const mojo_service,
                            SimpleRoutine::RoutineResultCallback callback) {
  auto* network_diagnostics_routines =
      mojo_service->GetNetworkDiagnosticsRoutines();
  if (!network_diagnostics_routines) {
    std::move(callback).Run({
        .status = mojom::DiagnosticRoutineStatusEnum::kNotRun,
        .status_message = kHttpFirewallRoutineNotRunMessage,
    });
    return;
  }
  network_diagnostics_routines->RunHttpFirewall(
      network_diagnostics_ipc::RoutineCallSource::kCrosHealthd,
      mojo::WrapCallbackWithDropHandler(
          base::BindOnce(&ParseHttpFirewallResult).Then(std::move(callback)),
          base::BindOnce(&PrintCallbackDropped)));
}

}  // namespace

const char kHttpFirewallRoutineNoProblemMessage[] =
    "HTTP firewall routine passed with no problems.";
const char kHttpFirewallRoutineHighDnsResolutionFailureRateProblemMessage[] =
    "DNS resolution failures above threshold.";
const char kHttpFirewallRoutineFirewallDetectedProblemMessage[] =
    "Firewall detected.";
const char kHttpFirewallRoutinePotentialFirewallProblemMessage[] =
    "A firewall may potentially exist.";
const char kHttpFirewallRoutineNotRunMessage[] =
    "HTTP firewall routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateHttpFirewallRoutine(
    MojoService* const mojo_service) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunHttpFirewallRoutine, mojo_service));
}

}  // namespace diagnostics
