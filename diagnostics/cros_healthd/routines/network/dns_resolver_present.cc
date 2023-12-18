// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/dns_resolver_present.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/bind.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

std::string GetProblemMessage(
    network_diagnostics_ipc::DnsResolverPresentProblem problem) {
  switch (problem) {
    case network_diagnostics_ipc::DnsResolverPresentProblem::
        kNoNameServersFound:
      return kDnsResolverPresentRoutineNoNameServersFoundProblemMessage;
    case network_diagnostics_ipc::DnsResolverPresentProblem::
        kMalformedNameServers:
      return kDnsResolverPresentRoutineMalformedNameServersProblemMessage;
    // DEPRECATED: Using kNoNamesServersFound response instead
    case network_diagnostics_ipc::DnsResolverPresentProblem::
        DEPRECATED_kEmptyNameServers:
      return kDnsResolverPresentRoutineNoNameServersFoundProblemMessage;
  }
}

// Parses the results of the DNS resolver present routine.
SimpleRoutine::RoutineResult ParseDnsResolverPresentResult(
    network_diagnostics_ipc::RoutineResultPtr result) {
  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kPassed,
          .status_message = kDnsResolverPresentRoutineNoProblemMessage,
      };
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kNotRun,
          .status_message = kDnsResolverPresentRoutineNotRunMessage,
      };
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      auto problems = result->problems->get_dns_resolver_present_problems();
      DCHECK(!problems.empty());
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kFailed,
          .status_message = GetProblemMessage(problems[0]),
      };
  }
}

void RunDnsResolverPresentRoutine(
    MojoService* const mojo_service,
    SimpleRoutine::RoutineResultCallback callback) {
  auto* network_diagnostics_routines =
      mojo_service->GetNetworkDiagnosticsRoutines();
  if (!network_diagnostics_routines) {
    std::move(callback).Run({
        .status = mojom::DiagnosticRoutineStatusEnum::kNotRun,
        .status_message = kDnsResolverPresentRoutineNotRunMessage,
    });
    return;
  }
  network_diagnostics_routines->RunDnsResolverPresent(
      base::BindOnce(&ParseDnsResolverPresentResult).Then(std::move(callback)));
}

}  // namespace

const char kDnsResolverPresentRoutineNoProblemMessage[] =
    "DNS resolver present routine passed with no problems.";
const char kDnsResolverPresentRoutineNoNameServersFoundProblemMessage[] =
    "IP config has no list of name servers available.";
const char kDnsResolverPresentRoutineMalformedNameServersProblemMessage[] =
    "IP config has a list of at least one malformed name server.";
const char kDnsResolverPresentRoutineNotRunMessage[] =
    "DNS resolver present routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateDnsResolverPresentRoutine(
    MojoService* const mojo_service) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunDnsResolverPresentRoutine, mojo_service));
}

}  // namespace diagnostics
