// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics.h"

#include <utility>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/http/http_transport.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <chromeos/net-base/ip_address.h>
#include <dbus/bus.h>

#include "shill/logging.h"
#include "shill/network/hosts_connectivity_diagnostics_util.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kConnectivityDiagnostics;
}  // namespace Logging

namespace {

constexpr std::string_view kPrefixHttp = "http://";
constexpr std::string_view kPrefixHttps = "https://";

// Default timeout. This should be enough to stop http request execution if the
// network experiences some sort of connectivity problems.
constexpr base::TimeDelta kDefaultConnectivityTimeout = base::Seconds(10);
// Maximum allowed timeout to avoid stalled state. This prohibits users' from
// setting too high timeout.
constexpr base::TimeDelta kMaxConnectivityTimeout = base::Seconds(60);

// Default error limit. 0 means no limit.
constexpr uint32_t kDefaultErrorLimit = 0;

}  // namespace

HostsConnectivityDiagnostics::HostsConnectivityDiagnostics(
    scoped_refptr<dbus::Bus> bus, std::string logging_tag)
    : bus_(bus), logging_tag_(std::move(logging_tag)) {}

HostsConnectivityDiagnostics::~HostsConnectivityDiagnostics() = default;

// static
base::TimeDelta HostsConnectivityDiagnostics::ParseTimeout(
    const KeyValueStore& options) {
  auto timeout_opt =
      options.GetOptionalValue<uint32_t>(kTestHostsConnectivityTimeoutKey);
  if (!timeout_opt.has_value()) {
    return kDefaultConnectivityTimeout;
  }

  uint32_t timeout_sec = timeout_opt.value();
  // Valid range is 1-60 seconds. Values of 0 or >60 fall back to default.
  if (timeout_sec >= 1 && timeout_sec <= kMaxConnectivityTimeout.InSeconds()) {
    return base::Seconds(timeout_sec);
  }

  return kDefaultConnectivityTimeout;
}

// static
uint32_t HostsConnectivityDiagnostics::ParseMaxErrorCount(
    const KeyValueStore& options) {
  return options.GetOptionalValue<uint32_t>(kTestHostsConnectivityMaxErrorsKey)
      .value_or(kDefaultErrorLimit);
}

// static
HostsConnectivityDiagnostics::ProxyOption
HostsConnectivityDiagnostics::ParseProxyOption(const KeyValueStore& options) {
  auto proxy_option =
      options.GetOptionalValue<std::string>(kTestHostsConnectivityProxyKey);
  if (!proxy_option.has_value()) {
    return ProxyOption{.mode = ProxyMode::kDirect};
  }
  const std::string& proxy_str = proxy_option.value();
  if (proxy_str == kTestHostsConnectivityProxyDirect) {
    return ProxyOption{.mode = ProxyMode::kDirect};
  }
  if (proxy_str == kTestHostsConnectivityProxySystem) {
    return ProxyOption{.mode = ProxyMode::kSystem};
  }
  return ProxyOption{.mode = ProxyMode::kCustom, .custom_url = proxy_str};
}

void HostsConnectivityDiagnostics::TestHostsConnectivity(
    RequestInfo request_info) {
  SLOG(2) << logging_tag_ << " " << __func__ << ": starting for "
          << request_info.raw_hostnames.size() << " hostnames";

  Request request;
  request.info = std::move(request_info);
  pending_requests_.emplace(std::move(request));

  if (!is_running_) {
    DispatchNextRequest();
  }
}

void HostsConnectivityDiagnostics::DispatchNextRequest() {
  if (pending_requests_.empty()) {
    is_running_ = false;
    return;
  }

  is_running_ = true;
  Request req = std::move(pending_requests_.front());
  pending_requests_.pop();

  NormalizeHostnames(std::move(req));
}

void HostsConnectivityDiagnostics::NormalizeHostnames(Request req) {
  if (req.info.raw_hostnames.empty()) {
    *req.response.add_connectivity_results() = CreateConnectivityResultEntry(
        /*hostname=*/std::nullopt, /*proxy=*/std::nullopt,
        hosts_connectivity_diagnostics::NO_VALID_HOSTNAME, kNoHostsProvided,
        /*resolution_message=*/std::nullopt,
        /*utc_timestamp_start=*/std::nullopt,
        /*utc_timestamp_end=*/std::nullopt);
    CompleteRequest(std::move(req));
    return;
  }

  for (const auto& raw_hostname : req.info.raw_hostnames) {
    std::optional<net_base::HttpUrl> url_hostname =
        ValidateAndNormalizeHostname(raw_hostname);
    if (url_hostname.has_value()) {
      HostnameTestSpec spec;
      spec.url_hostname = std::move(*url_hostname);
      req.specs.emplace_back(std::move(spec));
    } else {
      *req.response.add_connectivity_results() = CreateConnectivityResultEntry(
          raw_hostname, /*proxy=*/std::nullopt,
          hosts_connectivity_diagnostics::NO_VALID_HOSTNAME, kInvalidHostname,
          /*resolution_message=*/std::nullopt,
          /*utc_timestamp_start=*/std::nullopt,
          /*utc_timestamp_end=*/std::nullopt);
    }
  }

  if (req.specs.empty()) {
    CompleteRequest(std::move(req));
  } else {
    ValidateAndAssignProxy(std::move(req));
  }
}

void HostsConnectivityDiagnostics::ValidateAndAssignProxy(Request req) {
  CHECK(!req.specs.empty());

  if (req.info.proxy.mode == ProxyMode::kSystem) {
    // TODO(crbug.com/463098734): Resolve system proxy asynchronously via
    // GetChromeProxyServersAsync for each hostname. For now, fall through
    // to direct.
    RunConnectivityTests(std::move(req));
    return;
  }

  if (req.info.proxy.mode == ProxyMode::kCustom) {
    CHECK(req.info.proxy.custom_url.has_value());
    if (!IsValidProxyUrl(req.info.proxy.custom_url.value())) {
      *req.response.add_connectivity_results() = CreateConnectivityResultEntry(
          /*hostname=*/std::nullopt, req.info.proxy.custom_url,
          hosts_connectivity_diagnostics::NO_VALID_PROXY, kInvalidProxy,
          /*resolution_message=*/std::nullopt,
          /*utc_timestamp_start=*/std::nullopt,
          /*utc_timestamp_end=*/std::nullopt);
      CompleteRequest(std::move(req));
      return;
    }
  }

  const std::string proxy_url = req.info.proxy.mode == ProxyMode::kDirect
                                    ? std::string(brillo::http::kDirectProxy)
                                    : req.info.proxy.custom_url.value();
  for (auto& spec : req.specs) {
    spec.proxies = {proxy_url};
  }
  RunConnectivityTests(std::move(req));
}

// static
std::optional<net_base::HttpUrl>
HostsConnectivityDiagnostics::ValidateAndNormalizeHostname(
    const std::string& raw_hostname) {
  std::string hostname = raw_hostname;
  if (!base::StartsWith(raw_hostname, kPrefixHttp) &&
      !base::StartsWith(raw_hostname, kPrefixHttps)) {
    hostname = base::StrCat({kPrefixHttps, raw_hostname});
  }

  std::optional<net_base::HttpUrl> parsed_url =
      net_base::HttpUrl::CreateFromString(hostname);
  if (!parsed_url.has_value()) {
    LOG(WARNING) << __func__ << ": invalid hostname input: " << hostname;
    return std::nullopt;
  }

  // Reject URLs with paths or query parameters.
  // net_base::HttpUrl stores query params as part of path (e.g., "/?param").
  if (parsed_url->path() != "/") {
    LOG(WARNING) << __func__
                 << ": rejecting hostname with path or query parameters: "
                 << hostname;
    return std::nullopt;
  }

  // Reject URLs with userinfo (e.g., https://user@example.com).
  // Userinfo is a security risk as it can be used for phishing attacks
  // (e.g., https://google.com@evil.com appears to be google.com).
  // net_base::HttpUrl doesn't parse userinfo separately, so it ends up
  // in the host field.
  const std::string& host = parsed_url->host();
  if (host.find('@') != std::string::npos) {
    LOG(WARNING) << __func__
                 << ": rejecting hostname with userinfo: " << hostname;
    return std::nullopt;
  }

  // Reject IP addresses and localhost for security reasons.
  // Prevents access to RFC 1918 private ranges, localhost, and link-local
  // addresses.
  if (net_base::IPAddress::CreateFromString(host).has_value() ||
      base::EqualsCaseInsensitiveASCII(host, "localhost")) {
    LOG(WARNING) << __func__ << ": rejecting IP address or localhost: " << host;
    return std::nullopt;
  }

  return parsed_url;
}

void HostsConnectivityDiagnostics::RunConnectivityTests(Request req) {
  // Skeleton implementation: immediately return INTERNAL_ERROR.
  *req.response.add_connectivity_results() = CreateConnectivityResultEntry(
      /*hostname=*/std::nullopt, /*proxy=*/std::nullopt,
      hosts_connectivity_diagnostics::INTERNAL_ERROR, "Not implemented",
      /*resolution_message=*/std::nullopt,
      /*utc_timestamp_start=*/std::nullopt,
      /*utc_timestamp_end=*/std::nullopt);

  CompleteRequest(std::move(req));
}

// static
hosts_connectivity_diagnostics::ConnectivityResultEntry
HostsConnectivityDiagnostics::CreateConnectivityResultEntry(
    std::optional<std::string> hostname,
    std::optional<std::string> proxy,
    hosts_connectivity_diagnostics::ConnectivityResultCode result_code,
    std::optional<std::string_view> error_message,
    std::optional<std::string> resolution_message,
    std::optional<base::Time> utc_timestamp_start,
    std::optional<base::Time> utc_timestamp_end) {
  hosts_connectivity_diagnostics::ConnectivityResultEntry entry;
  entry.set_result_code(result_code);
  if (hostname.has_value()) {
    entry.set_hostname(std::move(*hostname));
  }
  if (proxy.has_value()) {
    entry.set_proxy(std::move(*proxy));
  }
  if (error_message.has_value()) {
    entry.set_error_message(std::string(*error_message));
  }
  if (resolution_message.has_value()) {
    entry.set_resolution_message(std::move(*resolution_message));
  }
  if (utc_timestamp_start.has_value()) {
    entry.set_timestamp_start(
        utc_timestamp_start->InMillisecondsSinceUnixEpoch());
  }
  if (utc_timestamp_end.has_value()) {
    entry.set_timestamp_end(utc_timestamp_end->InMillisecondsSinceUnixEpoch());
  }
  return entry;
}

void HostsConnectivityDiagnostics::CompleteRequest(Request req) {
  std::move(req.info.callback).Run(req.response);
  DispatchNextRequest();
}

}  // namespace shill
