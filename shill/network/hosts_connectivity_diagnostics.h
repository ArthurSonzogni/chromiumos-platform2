// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_
#define SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_

#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

#include "shill/store/key_value_store.h"

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

  // Proxy resolution mode for connectivity diagnostics.
  enum class ProxyMode {
    // No proxy, direct connection.
    kDirect,
    // Query Chrome for system proxy settings (async).
    kSystem,
    // User-provided proxy URL.
    kCustom,
  };

  // Parsed proxy option with explicit type discrimination.
  struct ProxyOption {
    ProxyMode mode = ProxyMode::kDirect;
    // Proxy URL. Has value when mode == kCustom.
    std::optional<std::string> custom_url = std::nullopt;
  };

  // Input parameters for a connectivity test request.
  struct RequestInfo {
    // List of hostnames/urls that needs to be validated and connection tested.
    std::vector<std::string> raw_hostnames;
    // Invoked with the TestConnectivityResponse when all tests complete.
    ConnectivityResultCallback callback;
    // Per-hostname HTTP HEAD timeout.
    base::TimeDelta timeout;
    // Stop testing after this many errors. 0 means no limit.
    uint32_t max_error_count = 0;
    // Proxy mode and optional custom URL.
    ProxyOption proxy;
  };

  HostsConnectivityDiagnostics(scoped_refptr<dbus::Bus> bus,
                               std::string logging_tag);
  HostsConnectivityDiagnostics(const HostsConnectivityDiagnostics&) = delete;
  HostsConnectivityDiagnostics& operator=(const HostsConnectivityDiagnostics&) =
      delete;
  ~HostsConnectivityDiagnostics();

  // Performs connectivity test on hostnames in `request_info`.
  void TestHostsConnectivity(RequestInfo request_info);

  // Parses the proxy option from user-provided options.
  // Returns kDirect if the option is not present or is "direct".
  // Returns kSystem if the value is "system".
  // Returns kCustom with the URL for any other value.
  static ProxyOption ParseProxyOption(const KeyValueStore& options);

  // Parses the timeout option from user-provided options.
  // Valid range is 1-60 seconds; values outside this range fall back to 10s.
  static base::TimeDelta ParseTimeout(const KeyValueStore& options);

  // Parses the max error count option from user-provided options.
  // Returns 0 (no limit) if the option is not present.
  static uint32_t ParseMaxErrorCount(const KeyValueStore& options);

 private:
  // Internal request with input data and accumulated results. Moved through
  // the pipeline by value.
  struct Request {
    RequestInfo info;
    // Accumulated results (validation errors and test results).
    hosts_connectivity_diagnostics::TestConnectivityResponse response;
  };

  // Dequeues and processes the next pending request, or sets `is_running_` to
  // false if the queue is empty.
  void DispatchNextRequest();

  // Runs connectivity tests for the request. Currently a skeleton that
  // returns INTERNAL_ERROR; will be replaced with actual implementation.
  void RunConnectivityTests(Request req);

  // Fires the callback with accumulated results and dispatches the next
  // queued request.
  void CompleteRequest(Request req);

  scoped_refptr<dbus::Bus> bus_;
  const std::string logging_tag_;

  // Queue of incoming requests waiting to be processed.
  std::queue<Request> pending_requests_;
  // True while a request is being processed (re-entrancy guard).
  bool is_running_ = false;

  base::WeakPtrFactory<HostsConnectivityDiagnostics> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_
