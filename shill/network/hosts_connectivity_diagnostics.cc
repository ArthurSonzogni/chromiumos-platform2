// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics.h"

#include <utility>

#include <base/logging.h>
#include <dbus/bus.h>

#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kConnectivityDiagnostics;
}  // namespace Logging

HostsConnectivityDiagnostics::HostsConnectivityDiagnostics(
    scoped_refptr<dbus::Bus> bus, std::string logging_tag)
    : bus_(bus), logging_tag_(std::move(logging_tag)) {}

HostsConnectivityDiagnostics::~HostsConnectivityDiagnostics() = default;

void HostsConnectivityDiagnostics::TestHostsConnectivity(
    RequestInfo request_info) {
  SLOG(2) << logging_tag_ << " " << __func__ << ": starting for "
          << request_info.raw_hostnames.size() << " hostnames";

  // Skeleton implementation: immediately return INTERNAL_ERROR.
  hosts_connectivity_diagnostics::TestConnectivityResponse response;
  auto* entry = response.add_connectivity_results();
  entry->set_result_code(hosts_connectivity_diagnostics::INTERNAL_ERROR);
  entry->set_error_message("Not implemented");

  std::move(request_info.callback).Run(response);
}

}  // namespace shill
