// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_network_diagnostics_routines.h"

#include <optional>
#include <string>
#include <utility>

#include <base/notimplemented.h>
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
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunLanConnectivityCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunSignalStrength(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunSignalStrengthCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunGatewayCanBePinged(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunGatewayCanBePingedCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunHasSecureWiFiConnection(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunHasSecureWiFiConnectionCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunDnsResolverPresent(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunDnsResolverPresentCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunDnsLatency(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunDnsLatencyCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunDnsResolution(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunDnsResolutionCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunCaptivePortal(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunCaptivePortalCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunHttpFirewall(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunHttpFirewallCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunHttpsFirewall(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunHttpsFirewallCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunHttpsLatency(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunHttpsLatencyCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunVideoConferencing(
    const std::optional<std::string>& stun_server_hostname,
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunVideoConferencingCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunArcHttp(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunArcHttpCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunArcPing(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunArcPingCallback callback) {
  std::move(callback).Run(result_->Clone());
}

void FakeNetworkDiagnosticsRoutines::RunArcDnsResolution(
    std::optional<network_diagnostics_ipc::RoutineCallSource> source,
    RunArcDnsResolutionCallback callback) {
  std::move(callback).Run(result_->Clone());
}

}  // namespace diagnostics
