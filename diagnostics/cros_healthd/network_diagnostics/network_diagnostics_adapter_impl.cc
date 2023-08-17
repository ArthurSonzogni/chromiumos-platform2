// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter_impl.h"

#include <optional>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_utils.h"

namespace diagnostics {

namespace {

namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

void LogCallbackDropped() {
  LOG(ERROR) << "Network diagnostics callback is dropped";
}

}  // namespace

NetworkDiagnosticsAdapterImpl::NetworkDiagnosticsAdapterImpl() = default;
NetworkDiagnosticsAdapterImpl::~NetworkDiagnosticsAdapterImpl() = default;

void NetworkDiagnosticsAdapterImpl::SetNetworkDiagnosticsRoutines(
    mojo::PendingRemote<network_diagnostics_ipc::NetworkDiagnosticsRoutines>
        network_diagnostics_routines) {
  network_diagnostics_routines_.reset();
  network_diagnostics_routines_.Bind(std::move(network_diagnostics_routines));
}

bool NetworkDiagnosticsAdapterImpl::ServiceRemoteBound() {
  return network_diagnostics_routines_.is_bound();
}

void NetworkDiagnosticsAdapterImpl::RunLanConnectivityRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunLanConnectivityCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewLanConnectivityProblems(
            {}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunLanConnectivity(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunSignalStrengthRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunSignalStrengthCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewSignalStrengthProblems(
            {}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunSignalStrength(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunGatewayCanBePingedRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunGatewayCanBePingedCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewGatewayCanBePingedProblems(
            {}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunGatewayCanBePinged(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunHasSecureWiFiConnectionRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunHasSecureWiFiConnectionCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(network_diagnostics_ipc::RoutineVerdict::kNotRun,
                               network_diagnostics_ipc::RoutineProblems::
                                   NewHasSecureWifiConnectionProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunHasSecureWiFiConnection(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunDnsResolverPresentRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunDnsResolverPresentCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewDnsResolverPresentProblems(
            {}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunDnsResolverPresent(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunDnsLatencyRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::RunDnsLatencyCallback
        callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewDnsLatencyProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunDnsLatency(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunDnsResolutionRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunDnsResolutionCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewDnsResolutionProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunDnsResolution(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunCaptivePortalRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunCaptivePortalCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewCaptivePortalProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunCaptivePortal(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunHttpFirewallRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::RunHttpFirewallCallback
        callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewHttpFirewallProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunHttpFirewall(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunHttpsFirewallRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunHttpsFirewallCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewHttpsFirewallProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunHttpsFirewall(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunHttpsLatencyRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::RunHttpsLatencyCallback
        callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewHttpsLatencyProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunHttpsLatency(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunVideoConferencingRoutine(
    const std::optional<std::string>& stun_server_hostname,
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunVideoConferencingCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewVideoConferencingProblems(
            {}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunVideoConferencing(
      stun_server_hostname,
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunArcHttpRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::RunArcHttpCallback
        callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewArcHttpProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunArcHttp(mojo::WrapCallbackWithDropHandler(
      std::move(callback), base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunArcPingRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::RunArcPingCallback
        callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewArcPingProblems({}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunArcPing(mojo::WrapCallbackWithDropHandler(
      std::move(callback), base::BindOnce(&LogCallbackDropped)));
}

void NetworkDiagnosticsAdapterImpl::RunArcDnsResolutionRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        RunArcDnsResolutionCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    auto result = CreateResult(
        network_diagnostics_ipc::RoutineVerdict::kNotRun,
        network_diagnostics_ipc::RoutineProblems::NewArcDnsResolutionProblems(
            {}));
    std::move(callback).Run(std::move(result));
    return;
  }
  network_diagnostics_routines_->RunArcDnsResolution(
      mojo::WrapCallbackWithDropHandler(std::move(callback),
                                        base::BindOnce(&LogCallbackDropped)));
}

}  // namespace diagnostics
