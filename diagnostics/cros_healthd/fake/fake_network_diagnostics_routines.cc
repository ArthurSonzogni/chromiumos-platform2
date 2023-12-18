// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_network_diagnostics_routines.h"

#include <utility>

#include <base/notreached.h>
#include <base/time/time.h>

#include "diagnostics/mojom/external/network_diagnostics.mojom.h"

namespace {
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;
}  // namespace

namespace diagnostics {

FakeNetworkDiagnosticsRoutines::FakeNetworkDiagnosticsRoutines() = default;

FakeNetworkDiagnosticsRoutines::~FakeNetworkDiagnosticsRoutines() = default;

void FakeNetworkDiagnosticsRoutines::SetRoutineResult(
    network_diagnostics_ipc::RoutineVerdict verdict,
    network_diagnostics_ipc::RoutineProblemsPtr problems) {
  result_ = network_diagnostics_ipc::RoutineResult::New();
  result_->verdict = verdict;
  result_->problems = std::move(problems);
  result_->timestamp = base::Time::Now();
}

void FakeNetworkDiagnosticsRoutines::GetResult(
    network_diagnostics_ipc::RoutineType routine, GetResultCallback callback) {
  NOTIMPLEMENTED();
}

void FakeNetworkDiagnosticsRoutines::GetAllResults(
    GetAllResultsCallback callback) {
  NOTIMPLEMENTED();
}

void FakeNetworkDiagnosticsRoutines::RunLanConnectivity(
    RunLanConnectivityCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunSignalStrength(
    RunSignalStrengthCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunGatewayCanBePinged(
    RunGatewayCanBePingedCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunHasSecureWiFiConnection(
    RunHasSecureWiFiConnectionCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunDnsResolverPresent(
    RunDnsResolverPresentCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunDnsLatency(
    RunDnsLatencyCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunDnsResolution(
    RunDnsResolutionCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunCaptivePortal(
    RunCaptivePortalCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunHttpFirewall(
    RunHttpFirewallCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunHttpsFirewall(
    RunHttpsFirewallCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunHttpsLatency(
    RunHttpsLatencyCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunVideoConferencing(
    const std::optional<std::string>& stun_server_hostname,
    RunVideoConferencingCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunArcHttp(RunArcHttpCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunArcPing(RunArcPingCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunArcDnsResolution(
    RunArcDnsResolutionCallback callback) {
  std::move(callback).Run(result_->Clone());
}

}  // namespace diagnostics
