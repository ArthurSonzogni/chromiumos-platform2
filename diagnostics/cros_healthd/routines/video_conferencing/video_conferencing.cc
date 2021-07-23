// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/video_conferencing/video_conferencing.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/notreached.h>
#include <base/strings/string_util.h>
#include <base/values.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

std::string GetProblemMessage(
    const std::vector<network_diagnostics_ipc::VideoConferencingProblem>&
        problems) {
  std::string problem_message = "";
  for (auto problem : problems) {
    switch (problem) {
      case network_diagnostics_ipc::VideoConferencingProblem::kUdpFailure:
        problem_message += (kVideoConferencingRoutineUdpFailureProblemMessage +
                            std::string("\n"));
        break;
      case network_diagnostics_ipc::VideoConferencingProblem::kTcpFailure:
        problem_message += (kVideoConferencingRoutineTcpFailureProblemMessage +
                            std::string("\n"));
        break;
      case network_diagnostics_ipc::VideoConferencingProblem::kMediaFailure:
        problem_message +=
            (kVideoConferencingRoutineMediaFailureProblemMessage +
             std::string("\n"));
        break;
    }
  }

  return base::TrimString(problem_message, "\n", base::TRIM_TRAILING)
      .as_string();
}

// Parses the results of the video conferencing routine.
void ParseVideoConferencingResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineResultPtr result) {
  DCHECK(status);
  DCHECK(status_message);

  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kVideoConferencingRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kVideoConferencingRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      auto problems = result->problems->get_video_conferencing_problems();
      DCHECK(!problems.empty());
      *status_message = GetProblemMessage(problems);
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the video
// conferencing routine never includes an output.
void RunVideoConferencingRoutine(
    const base::Optional<std::string>& stun_server_hostname,
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunVideoConferencingRoutine(
      stun_server_hostname,
      base::BindOnce(&ParseVideoConferencingResult, status, status_message));
}

}  // namespace

const char kVideoConferencingRoutineNoProblemMessage[] =
    "Video conferencing routine passed with no problems.";
const char kVideoConferencingRoutineUdpFailureProblemMessage[] =
    "Failed requests to a STUN server via UDP.";
const char kVideoConferencingRoutineTcpFailureProblemMessage[] =
    "Failed requests to a STUN server via TCP.";
const char kVideoConferencingRoutineMediaFailureProblemMessage[] =
    "Failed to establish a TLS connection to media hostnames.";
const char kVideoConferencingRoutineNotRunMessage[] =
    "Video conferencing routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateVideoConferencingRoutine(
    const base::Optional<std::string>& stun_server_hostname,
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunVideoConferencingRoutine, stun_server_hostname,
                     network_diagnostics_adapter));
}

}  // namespace diagnostics
