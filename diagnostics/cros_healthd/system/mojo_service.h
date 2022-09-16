// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_H_

namespace chromeos::mojo_service_manager::mojom {
class ServiceManager;
}

namespace chromeos::cros_healthd::internal::mojom {
class ChromiumDataCollector;
}

namespace chromeos::network_health::mojom {
class NetworkHealthService;
}

namespace chromeos::network_diagnostics::mojom {
class NetworkDiagnosticsRoutines;
}

namespace diagnostics {

// Interface for accessing external mojo services.
// TODO(b/237239654): Move network mojo interface here and clean up the network
// adaptors.
class MojoService {
 public:
  virtual ~MojoService() = default;

  // Returns the mojo interface to ServiceManager.
  virtual chromeos::mojo_service_manager::mojom::ServiceManager*
  GetServiceManager() = 0;

  // Returns the mojo interface to ChromiumDataCollector.
  virtual chromeos::cros_healthd::internal::mojom::ChromiumDataCollector*
  GetChromiumDataCollector() = 0;

  // Returns the mojo interface to NetworkHealthService.
  virtual chromeos::network_health::mojom::NetworkHealthService*
  GetNetworkHealth() = 0;

  // Returns the mojo interface to NetworkDiagnosticsRoutines.
  virtual chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines*
  GetNetworkDiagnosticsRoutines() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_H_
