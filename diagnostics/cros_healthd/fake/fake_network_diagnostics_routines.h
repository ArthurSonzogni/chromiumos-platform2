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
  void RunLanConnectivity(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunLanConnectivityCallback callback) override;
  void RunSignalStrength(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunSignalStrengthCallback callback) override;
  void RunGatewayCanBePinged(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunGatewayCanBePingedCallback callback) override;
  void RunHasSecureWiFiConnection(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunHasSecureWiFiConnectionCallback callback) override;
  void RunDnsResolverPresent(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunDnsResolverPresentCallback callback) override;
  void RunDnsLatency(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunDnsLatencyCallback callback) override;
  void RunDnsResolution(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunDnsResolutionCallback callback) override;
  void RunCaptivePortal(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunCaptivePortalCallback callback) override;
  void RunHttpFirewall(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunHttpFirewallCallback callback) override;
  void RunHttpsFirewall(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunHttpsFirewallCallback callback) override;
  void RunHttpsLatency(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunHttpsLatencyCallback callback) override;
  void RunVideoConferencing(
      const std::optional<std::string>& stun_server_hostname,
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunVideoConferencingCallback callback) override;
  void RunArcHttp(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunArcHttpCallback callback) override;
  void RunArcPing(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunArcPingCallback callback) override;
  void RunArcDnsResolution(
      std::optional<chromeos::network_diagnostics::mojom::RoutineCallSource>
          source,
      RunArcDnsResolutionCallback callback) override;

  // Mojo receiver for binding pipe.
  mojo::Receiver<
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
      receiver_{this};

  // The fake result of routine.
  chromeos::network_diagnostics::mojom::RoutineResultPtr result_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_NETWORK_DIAGNOSTICS_ROUTINES_H_
