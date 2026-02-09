// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics.h"

#include <utility>

#include <base/logging.h>
#include <base/time/time.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <dbus/bus.h>

#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kConnectivityDiagnostics;
}  // namespace Logging

namespace {

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

  RunConnectivityTests(std::move(req));
}

void HostsConnectivityDiagnostics::RunConnectivityTests(Request req) {
  // Skeleton implementation: immediately return INTERNAL_ERROR.
  auto* entry = req.response.add_connectivity_results();
  entry->set_result_code(hosts_connectivity_diagnostics::INTERNAL_ERROR);
  entry->set_error_message("Not implemented");

  CompleteRequest(std::move(req));
}

void HostsConnectivityDiagnostics::CompleteRequest(Request req) {
  std::move(req.info.callback).Run(req.response);
  DispatchNextRequest();
}

}  // namespace shill
