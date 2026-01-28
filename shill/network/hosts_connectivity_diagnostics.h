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
#include <brillo/variant_dictionary.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

namespace dbus {
class Bus;
}  // namespace dbus

namespace shill {

// Tests network connectivity to a list of hostnames with configurable proxy
// and timeout options. Results are returned as a protobuf message.
class HostsConnectivityDiagnostics {
 public:
  HostsConnectivityDiagnostics(scoped_refptr<dbus::Bus> bus,
                               std::string logging_tag);
  HostsConnectivityDiagnostics(const HostsConnectivityDiagnostics&) = delete;
  HostsConnectivityDiagnostics& operator=(const HostsConnectivityDiagnostics&) =
      delete;
  ~HostsConnectivityDiagnostics();

  // Callback invoked with connectivity test results. The response contains
  // a ConnectivityResult entry for each tested hostname, with result_code
  // indicating success or the type of failure encountered (see the
  // `hosts_connectivity_diagnostics.proto` for more details).
  using ConnectivityResultCallback = base::OnceCallback<void(
      const hosts_connectivity_diagnostics::TestConnectivityResponse&
          response)>;
  void TestHostsConnectivity(const std::vector<std::string>& hostnames,
                             const brillo::VariantDictionary& options,
                             ConnectivityResultCallback result_callback);

 private:
  scoped_refptr<dbus::Bus> bus_;
  const std::string logging_tag_;

  base::WeakPtrFactory<HostsConnectivityDiagnostics> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_
