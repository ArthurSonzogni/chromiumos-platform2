// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_UTIL_H_
#define SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_UTIL_H_

#include <string>

#include <curl/curl.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

namespace shill {

// Maps CURLcode to ConnectivityResultCode for connectivity diagnostics.
// Returns `ResultCode::UNKNOWN_ERROR` if invalid or unknown CURLcode is
// given.
hosts_connectivity_diagnostics::ConnectivityResultCode
CurlErrorToConnectivityResultCode(CURLcode curl_result);

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
