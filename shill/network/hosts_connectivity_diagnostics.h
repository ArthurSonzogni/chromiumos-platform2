// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_
#define SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_

#include <memory>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

namespace dbus {
class Bus;
}  // namespace dbus

namespace shill {

// Tests network connectivity to a list of hostnames with configurable proxy
// and timeout options. Results are returned as a protobuf message.
class HostsConnectivityDiagnostics {
 public:
  // Callback invoked with connectivity test results. The response contains
  // a ConnectivityResult entry for each tested hostname, with result_code
  // indicating success or the type of failure encountered (see the
  // `hosts_connectivity_diagnostics.proto` for more details).
  using ConnectivityResultCallback = base::OnceCallback<void(
      const hosts_connectivity_diagnostics::TestConnectivityResponse&
          response)>;

  // Input parameters for a connectivity test request.
  struct RequestInfo {
    // List of hostnames/urls that needs to be validated and connection tested.
    std::vector<std::string> raw_hostnames;
    // Invoked with the TestConnectivityResponse when all tests complete.
    ConnectivityResultCallback callback;
  };

  HostsConnectivityDiagnostics(scoped_refptr<dbus::Bus> bus,
                               std::string logging_tag);
  HostsConnectivityDiagnostics(const HostsConnectivityDiagnostics&) = delete;
  HostsConnectivityDiagnostics& operator=(const HostsConnectivityDiagnostics&) =
      delete;
  ~HostsConnectivityDiagnostics();

  // Performs connectivity test on hostnames in `request_info`.
  void TestHostsConnectivity(RequestInfo request_info);

 private:
  scoped_refptr<dbus::Bus> bus_;
  const std::string logging_tag_;

  base::WeakPtrFactory<HostsConnectivityDiagnostics> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_
