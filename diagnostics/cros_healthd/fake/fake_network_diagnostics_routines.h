// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_NETWORK_DIAGNOSTICS_ROUTINES_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_NETWORK_DIAGNOSTICS_ROUTINES_H_

#include <optional>
#include <string>

#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/mojom/external/network_diagnostics.mojom.h"

namespace diagnostics {

// Fake implementation of NetworkDiagnosticsRoutines.
class FakeNetworkDiagnosticsRoutines
    : public chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines {
 public:
  FakeNetworkDiagnosticsRoutines();
  FakeNetworkDiagnosticsRoutines(const FakeNetworkDiagnosticsRoutines&) =
      delete;
  FakeNetworkDiagnosticsRoutines& operator=(
      const FakeNetworkDiagnosticsRoutines&) = delete;
  ~FakeNetworkDiagnosticsRoutines() override;

  // Modifiers.
  mojo::Receiver<
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>&
  receiver() {
    return receiver_;
  }

  void SetRoutineResult(
      chromeos::network_diagnostics::mojom::RoutineVerdict verdict,
      chromeos::network_diagnostics::mojom::RoutineProblemsPtr problems);

 private:
  // chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines overrides.
  void GetResult(chromeos::network_diagnostics::mojom::RoutineType routine,
                 GetResultCallback callback) override;
  void GetAllResults(GetAllResultsCallback callback) override;
  void RunLanConnectivity(RunLanConnectivityCallback callback) override;
  void RunSignalStrength(RunSignalStrengthCallback callback) override;
  void RunGatewayCanBePinged(RunGatewayCanBePingedCallback callback) override;
  void RunHasSecureWiFiConnection(
      RunHasSecureWiFiConnectionCallback callback) override;
  void RunDnsResolverPresent(RunDnsResolverPresentCallback callback) override;
  void RunDnsLatency(RunDnsLatencyCallback callback) override;
  void RunDnsResolution(RunDnsResolutionCallback callback) override;
  void RunCaptivePortal(RunCaptivePortalCallback callback) override;
  void RunHttpFirewall(RunHttpFirewallCallback callback) override;
  void RunHttpsFirewall(RunHttpsFirewallCallback callback) override;
  void RunHttpsLatency(RunHttpsLatencyCallback callback) override;
  void RunVideoConferencing(
      const std::optional<std::string>& stun_server_hostname,
      RunVideoConferencingCallback callback) override;
  void RunArcHttp(RunArcHttpCallback callback) override;
  void RunArcPing(RunArcPingCallback callback) override;
  void RunArcDnsResolution(RunArcDnsResolutionCallback callback) override;

  // Mojo receiver for binding pipe.
  mojo::Receiver<
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
      receiver_{this};

  // The fake result of routine.
  chromeos::network_diagnostics::mojom::RoutineResultPtr result_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_NETWORK_DIAGNOSTICS_ROUTINES_H_
