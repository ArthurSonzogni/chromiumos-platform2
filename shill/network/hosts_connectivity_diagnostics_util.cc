// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics_util.h"

#include <algorithm>
#include <vector>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace shill {

namespace {

constexpr std::string_view kPrefixHttp = "http://";
constexpr std::string_view kPrefixHttps = "https://";
constexpr std::string_view kPrefixSocks4 = "socks4://";
constexpr std::string_view kPrefixSocks5 = "socks5://";

constexpr int kMaxPortNumber = 65535;

}  // namespace

hosts_connectivity_diagnostics::ConnectivityResultCode
TransportErrorToConnectivityResultCode(brillo::http::TransportError error) {
  using ResultCode = hosts_connectivity_diagnostics::ConnectivityResultCode;
  using TE = brillo::http::TransportError;

  switch (error) {
    case TE::kDnsFailure:
    case TE::kDnsTimeout:
      return ResultCode::DNS_RESOLUTION_ERROR;
    case TE::kProxyDnsFailure:
      return ResultCode::PROXY_DNS_RESOLUTION_ERROR;
    case TE::kProxyConnectionFailure:
      return ResultCode::PROXY_CONNECTION_FAILURE;
    case TE::kConnectionFailure:
      return ResultCode::CONNECTION_FAILURE;
    case TE::kTimeout:
      return ResultCode::CONNECTION_TIMEOUT;
    case TE::kTLSFailure:
      return ResultCode::SSL_CONNECTION_ERROR;
    case TE::kCertificateError:
      return ResultCode::PEER_CERTIFICATE_ERROR;
    case TE::kHttpError:
      return ResultCode::HTTP_ERROR;
    case TE::kNetworkError:
      return ResultCode::NO_NETWORK_ERROR;
    case TE::kInternalError:
    case TE::kBackendFailed:
      return ResultCode::INTERNAL_ERROR;
    case TE::kIOError:
    case TE::kUnknown:
      return ResultCode::UNKNOWN_ERROR;
  }
  return ResultCode::UNKNOWN_ERROR;
}

bool IsValidProxyUrl(const std::string& proxy_url) {
  if (!base::IsStringUTF8(proxy_url)) {
    return false;
  }

  static const std::vector<std::string_view> kValidProxySchemes = {
      kPrefixHttp,
      kPrefixHttps,
      kPrefixSocks4,
      kPrefixSocks5,
  };

  auto scheme_it = std::find_if(
      kValidProxySchemes.begin(), kValidProxySchemes.end(),
      [&proxy_url](std::string_view scheme) {
        return base::StartsWith(proxy_url, scheme,
                                base::CompareCase::INSENSITIVE_ASCII);
      });

  if (scheme_it == kValidProxySchemes.end()) {
    return false;
  }

  // Extract the authority component (everything after the scheme).
  // Example: "http://user:pass@proxy.com:8080" -> "user:pass@proxy.com:8080"
  std::string authority = proxy_url.substr(scheme_it->length());

  // Reject empty authority (e.g., "http://" or "socks5://").
  if (authority.empty()) {
    return false;
  }

  // Reject path, query, or fragment components. Proxy URLs should only
  // contain authority (host:port with optional userinfo), not resource paths.
  // Examples of rejected URLs:
  //   "http://proxy.com:8080/path" (path not allowed)
  //   "http://proxy.com?query"     (query not allowed)
  //   "http://proxy.com#fragment"  (fragment not allowed)
  constexpr std::string_view kInvalidDelimiters = "/#?";
  if (authority.find_first_of(kInvalidDelimiters) != std::string::npos) {
    return false;
  }

  // Extract host_and_port, skipping optional userinfo (user[:pass]@).
  // Userinfo provides proxy authentication credentials and is valid per
  // GetChromeProxyServersAsync format: scheme://[[user:pass@]host:port].
  // We use rfind('@') to handle passwords containing '@' characters.
  // Examples:
  //   "user:pass@proxy.com:8080"       -> host_and_port = "proxy.com:8080"
  //   "user:p@ss@proxy.com:8080"       -> host_and_port = "proxy.com:8080"
  //   "proxy.com:8080"                 -> host_and_port = "proxy.com:8080"
  std::string host_and_port;
  size_t at_pos = authority.rfind('@');
  if (at_pos != std::string::npos) {
    // Userinfo present - extract host:port after '@'.
    // Userinfo itself is not validated beyond UTF-8 (already checked).
    host_and_port = authority.substr(at_pos + 1);
    if (host_and_port.empty()) {
      return false;
    }
  } else {
    host_and_port = authority;
  }

  std::string host;
  std::string port_str;

  // Handle IPv6 literals enclosed in brackets per RFC 3986.
  // Examples:
  //   "[::1]:8080"        -> host = "::1", port_str = "8080"
  //   "[2001:db8::1]"     -> host = "2001:db8::1", port_str = ""
  //   "[::1]"             -> host = "::1", port_str = ""
  // Rejected:
  //   "["                 -> missing closing bracket
  //   "[]"                -> empty host
  //   "[::1]extra"        -> invalid chars after bracket (must be ':' or end)
  if (!host_and_port.empty() && host_and_port[0] == '[') {
    size_t bracket_end = host_and_port.find(']');
    if (bracket_end == std::string::npos) {
      return false;
    }

    host = host_and_port.substr(1, bracket_end - 1);
    if (host.empty()) {
      return false;
    }

    std::string_view remainder =
        std::string_view(host_and_port).substr(bracket_end + 1);
    if (!remainder.empty()) {
      if (remainder[0] != ':') {
        return false;
      }
      port_str = std::string(remainder.substr(1));
    }
  } else {
    // IPv4 address or hostname format.
    // Reject multiple colons without brackets as this is ambiguous
    // (could be malformed IPv6 or host:port with extra colons).
    // Examples:
    //   "proxy.com:8080"  -> host = "proxy.com", port_str = "8080"
    //   "192.168.1.1:80"  -> host = "192.168.1.1", port_str = "80"
    //   "proxy.com"       -> host = "proxy.com", port_str = ""
    // Rejected:
    //   "::1:8080"        -> ambiguous (unbracketed IPv6 with port?)
    size_t colon_pos = host_and_port.rfind(':');
    size_t first_colon = host_and_port.find(':');
    if (first_colon != std::string::npos && first_colon != colon_pos) {
      return false;
    }

    if (colon_pos != std::string::npos) {
      host = host_and_port.substr(0, colon_pos);
      port_str = host_and_port.substr(colon_pos + 1);
    } else {
      host = host_and_port;
    }
  }

  // Reject empty host (e.g., "http://user:pass@" or "http://:8080").
  if (host.empty()) {
    return false;
  }

  // Validate port is numeric and in valid range (1-65535).
  // Port is optional - if not specified, the client uses scheme defaults.
  // Rejected ports:
  //   "abc"   -> non-numeric
  //   "0"     -> below valid range
  //   "65536" -> above valid range (max is 65535)
  //   "-1"    -> negative
  if (!port_str.empty()) {
    int port;
    if (!base::StringToInt(port_str, &port)) {
      return false;
    }
    if (port < 1 || port > kMaxPortNumber) {
      return false;
    }
  }

  return true;
}

}  // namespace shill
