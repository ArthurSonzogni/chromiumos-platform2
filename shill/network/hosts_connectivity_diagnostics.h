// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_
#define SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_

#include <deque>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <brillo/http/http_proxy.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

#include "shill/mockable.h"
#include "shill/network/http_request.h"
#include "shill/store/key_value_store.h"

namespace dbus {
class Bus;
}  // namespace dbus

namespace shill {

class EventDispatcher;

inline constexpr std::string_view kNoHostsProvided =
    "No hosts provided for connectivity diagnostics.";
inline constexpr std::string_view kInvalidHostname =
    "Provided hostname is invalid. It must be a domain name (e.g., "
    "hostname.domain) with http:// or https:// prefix (other prefixes are not "
    "allowed). IP addresses and localhost are not allowed for security "
    "reasons.";
inline constexpr std::string_view kInvalidProxy =
    "Provided proxy is invalid. It must be a valid URL with http://, https://, "
    "socks4://, or socks5:// scheme followed by a host (and optional port).";
inline constexpr std::string_view kUnableToGetSystemProxy =
    "Unable to determine system setup proxy.";
inline constexpr std::string_view kUnableToGetSystemProxyResolution =
    "Please check your proxy configuration.";

// Tests network connectivity to a list of hostnames with configurable proxy
// and timeout options. Results are returned as a protobuf message.
//
// Request Processing Flow:
// ========================
//
// 1. QUEUING: TestHostsConnectivity() receives hostnames + options, wraps them
//    in a Request, and queues it. Only one request executes at a time.
//
// 2. PIPELINE: DispatchNextRequest() dequeues a request and passes it through:
//    NormalizeHostnames -> ValidateAndAssignProxy -> RunConnectivityTests ->
//    CompleteRequest. For "system" proxy mode, ValidateAndAssignProxy starts
//    async per-hostname proxy resolution via ResolveNextSystemProxy before
//    RunConnectivityTests.
//
// 3. EXECUTION PHASE: Request::specs is a deque of HostnameTestSpec, each
//    holding a deque of proxy URLs. RunNextConnectivityTest() pops the front
//    proxy from the front spec, creates an HttpRequest, and calls Start() with
//    Method::kHead. OnHttpRequestComplete() records the result and re-enters
//    RunNextConnectivityTest(). Specs whose proxy deques are exhausted are
//    popped from the front.
//
// 4. COMPLETION: Results are returned via callback as protobuf.
//    Then DispatchNextRequest() starts the next queued request if any.
class HostsConnectivityDiagnostics {
 public:
  // Callback invoked with connectivity test results. The response contains
  // a ConnectivityResult entry for each tested hostname, with result_code
  // indicating success or the type of failure encountered (see the
  // `hosts_connectivity_diagnostics.proto` for more details).
  using ConnectivityResultCallback = base::OnceCallback<void(
      const hosts_connectivity_diagnostics::TestConnectivityResponse&
          response)>;

  // Callback type for Chrome proxy resolution.
  using GetProxyCallback = brillo::http::GetChromeProxyServersCallback;
  using GetProxyFunction = base::RepeatingCallback<void(
      const std::string& url, GetProxyCallback callback)>;

  // Factory callback that creates a new HttpRequest for each connectivity
  // test. The factory receives `transport` to support per-request proxy
  // configuration. Can be overridden in tests via
  // `SetHttpRequestFactoryForTest()`.
  using HttpRequestFactory =
      base::RepeatingCallback<std::unique_ptr<HttpRequest>(
          std::shared_ptr<brillo::http::Transport> transport)>;

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
    // Network interface to bind DNS/HTTP to (e.g., "eth0", "wlan0").
    std::string interface_name;
    // IPv4 vs IPv6 DNS resolution.
    net_base::IPFamily ip_family = net_base::IPFamily::kIPv4;
    // DNS servers to use for name resolution.
    std::vector<net_base::IPAddress> dns_list;
  };

  // Constructs a long-lived HostsConnectivityDiagnostics instance.
  //
  // `dispatcher` is the event dispatcher for async operations.
  // `bus` is the D-Bus connection for Chrome proxy resolution.
  // `logging_tag` identifies this instance in log messages.
  HostsConnectivityDiagnostics(EventDispatcher* dispatcher,
                               scoped_refptr<dbus::Bus> bus,
                               std::string logging_tag);
  HostsConnectivityDiagnostics(const HostsConnectivityDiagnostics&) = delete;
  HostsConnectivityDiagnostics& operator=(const HostsConnectivityDiagnostics&) =
      delete;
  virtual ~HostsConnectivityDiagnostics();

  // Performs connectivity test on hostnames in `request_info`. Network context
  // (interface name, IP family, DNS servers) is supplied per-request via
  // `RequestInfo` so that each call can target a different network.
  mockable void TestHostsConnectivity(RequestInfo request_info);

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

  // Sets the proxy resolution function for testing purposes.
  void SetGetProxyFunctionForTest(GetProxyFunction func);

  // Overrides the HttpRequest factory for testing. `factory` is called once
  // per connectivity test with the transport to use.
  void SetHttpRequestFactoryForTest(HttpRequestFactory factory);

 private:
  // Single hostname ready for connectivity testing.
  struct HostnameTestSpec {
    // Validated and normalized URL to test connectivity against.
    net_base::HttpUrl url_hostname;
    // List of proxy URLs to use for this hostname (e.g., "direct://",
    // "http://proxy:8080"). Each proxy will be tested sequentially.
    std::deque<std::string> proxies;
  };

  // Internal request with input data and accumulated results. Moved through
  // the pipeline by value.
  struct Request {
    RequestInfo info;

    // Hostnames ready for connectivity testing. Populated by
    // NormalizeHostnames, consumed (popped from front) by
    // RunNextConnectivityTest.
    std::deque<HostnameTestSpec> specs;
    // Accumulated results (validation errors and test results).
    hosts_connectivity_diagnostics::TestConnectivityResponse response;

    // Number of failed tests so far. Compared against
    // info.max_error_count to decide whether to abort early.
    uint32_t error_count = 0;
  };

  // Dequeues and processes the next pending request, or sets `is_running_` to
  // false if the queue is empty.
  void DispatchNextRequest();

  // Populates `req.specs` from raw hostnames. If the hostname list is empty,
  // records a NO_VALID_HOSTNAME error and completes the request.
  // Otherwise calls ValidateAndAssignProxy.
  void NormalizeHostnames(Request req);

  // Validates the proxy option. On invalid proxy, fires the
  // `ConnectivityResultCallback` object of the given `Request` with
  // NO_VALID_PROXY and and dispatches the next request if any. Otherwise
  // resolves the proxy URL into `req.info.proxy.custom_url` and calls
  // RunConnectivityTests (or StartSystemProxyResolution for system proxy mode).
  void ValidateAndAssignProxy(Request req);

  // --- System proxy resolution (called from ValidateAndAssignProxy) ---

  // Separate the `HostnameTestSpec` queue from the `Request` and starts
  // resolution with `ResolveNextSystemProxy()`.
  void StartSystemProxyResolution(Request req);

  // Resolves system proxy for the next unresolved hostname. Pops one spec
  // from |unresolved_specs|, resolves via async callback, and pushes into
  // req.specs on success. Once |unresolved_specs| is empty, calls
  // RunConnectivityTests.
  void ResolveNextSystemProxy(Request req,
                              std::deque<HostnameTestSpec> unresolved_specs);

  // Callback for system proxy resolution of a single hostname.
  void OnSystemProxyResolved(Request req,
                             std::deque<HostnameTestSpec> unresolved_specs,
                             HostnameTestSpec spec,
                             bool success,
                             const std::vector<std::string>& proxies);

  // --- Connectivity test execution ---

  // Resets error_count and starts the test loop.
  void RunConnectivityTests(Request req);

  // Pops the front proxy from the front spec and fires one HTTP HEAD test.
  // Specs with no remaining proxies are skipped.
  void RunNextConnectivityTest(Request req);

  // Callback invoked when the HttpRequest for a single connectivity test
  // completes. Converts `result` to a ConnectivityResultCode and calls
  // OnSingleTestComplete.
  void OnHttpRequestComplete(Request req,
                             std::string hostname,
                             std::string proxy,
                             base::Time test_start_time,
                             HttpRequest::Result result);

  // Handles completion of a single connectivity test. Records the result
  // and re-enters RunNextConnectivityTest, or aborts on max_error_count.
  void OnSingleTestComplete(
      Request req,
      std::string hostname,
      std::string proxy,
      base::Time test_start_time,
      hosts_connectivity_diagnostics::ConnectivityResultCode result_code,
      const std::string& error_message);

  // Fires the callback with accumulated results and dispatches the next
  // queued request.
  void CompleteRequest(Request req);

  // Validates and normalizes a hostname. Adds https:// prefix if no scheme
  // is present. Rejects paths, query parameters, userinfo, IP addresses,
  // and localhost. Returns nullopt if the hostname is invalid.
  static std::optional<net_base::HttpUrl> ValidateAndNormalizeHostname(
      const std::string& raw_hostname);

  // Creates a single ConnectivityResultEntry protobuf with the given fields.
  // Optional parameters that are nullopt are left unset in the proto.
  static hosts_connectivity_diagnostics::ConnectivityResultEntry
  CreateConnectivityResultEntry(
      std::optional<std::string> hostname,
      std::optional<std::string> proxy,
      hosts_connectivity_diagnostics::ConnectivityResultCode result_code,
      std::optional<std::string_view> error_message,
      std::optional<std::string> resolution_message,
      std::optional<base::Time> utc_timestamp_start,
      std::optional<base::Time> utc_timestamp_end);

  // Calls the proxy resolution function (either real or injected for testing).
  void GetChromeProxyServersAsync(const std::string& url,
                                  GetProxyCallback callback);

  // Creates a new HttpRequest. Uses `http_request_factory_for_testing_` if
  // set, otherwise `http_request_factory_`.
  std::unique_ptr<HttpRequest> CreateHttpRequest(
      std::shared_ptr<brillo::http::Transport> transport);

  EventDispatcher* const dispatcher_;
  const std::string logging_tag_;

  // Queue of incoming requests waiting to be processed.
  std::queue<Request> pending_requests_;
  // True while a request is being processed (re-entrancy guard).
  bool is_running_ = false;

  // The HttpRequest currently in-flight. Null when no test is running.
  std::unique_ptr<HttpRequest> active_http_request_;

  // Resolves proxy servers for a URL via Chrome's proxy resolution engine
  // (D-Bus call to Chrome). Can be overridden for testing.
  GetProxyFunction get_proxy_function_;

  // Factory for creating HttpRequest instances. Rebuilt per request in
  // DispatchNextRequest() with the request's network context.
  HttpRequestFactory http_request_factory_;

  // Test-only override. When set, CreateHttpRequest() uses this instead
  // of `http_request_factory_`.
  HttpRequestFactory http_request_factory_for_testing_;

  base::WeakPtrFactory<HostsConnectivityDiagnostics> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_H_
