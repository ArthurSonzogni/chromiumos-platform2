// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_UTIL_H_
#define SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_UTIL_H_

#include <string>

#include <brillo/http/http_transport_error.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

namespace shill {

// Converts `error` to a ConnectivityResultCode for diagnostics
// reporting. Returns UNKNOWN_ERROR for kIOError and kUnknown.
hosts_connectivity_diagnostics::ConnectivityResultCode
TransportErrorToConnectivityResultCode(brillo::http::TransportError error);

// Validates a user-provided proxy URL.
// Valid formats: scheme://[[user[:pass]@]host[:port]
// Where scheme is one of: http, https, socks4, socks5
//
// Validation includes:
// - Scheme allowlist (case-insensitive)
// - Optional userinfo (credentials) for proxy authentication
// - Non-empty host after scheme
// - Numeric port in valid range (1-65535) if specified
// - No path, query, or fragment components
// - IPv6 literal support with brackets
// - UTF-8 validation
bool IsValidProxyUrl(const std::string& proxy_url);

}  // namespace shill

#endif  // SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_UTIL_H_
