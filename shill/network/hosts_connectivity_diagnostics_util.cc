// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics_util.h"

#include <algorithm>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <base/containers/map_util.h>
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
CurlErrorToConnectivityResultCode(CURLcode curl_result) {
  using ResultCode = hosts_connectivity_diagnostics::ConnectivityResultCode;

  static constexpr auto kCurlErrorMap =
      base::MakeFixedFlatMap<CURLcode, ResultCode>({
          // Success
          {CURLE_OK, ResultCode::SUCCESS},

          // DNS Resolution Errors
          {CURLE_COULDNT_RESOLVE_HOST, ResultCode::DNS_RESOLUTION_ERROR},

          // Proxy DNS Resolution Errors
          {CURLE_COULDNT_RESOLVE_PROXY, ResultCode::PROXY_DNS_RESOLUTION_ERROR},

          // Proxy Connection Failures
          {CURLE_PROXY, ResultCode::PROXY_CONNECTION_FAILURE},

          // General Connection Failures
          {CURLE_COULDNT_CONNECT, ResultCode::CONNECTION_FAILURE},
          {CURLE_GOT_NOTHING, ResultCode::CONNECTION_FAILURE},

          // Timeout Errors
          {CURLE_OPERATION_TIMEDOUT, ResultCode::CONNECTION_TIMEOUT},

          // SSL/TLS Connection Errors
          {CURLE_SSL_CONNECT_ERROR, ResultCode::SSL_CONNECTION_ERROR},
          {CURLE_SSL_ENGINE_NOTFOUND, ResultCode::SSL_CONNECTION_ERROR},
          {CURLE_SSL_ENGINE_SETFAILED, ResultCode::SSL_CONNECTION_ERROR},
          {CURLE_SSL_ENGINE_INITFAILED, ResultCode::SSL_CONNECTION_ERROR},
          {CURLE_SSL_CIPHER, ResultCode::SSL_CONNECTION_ERROR},
          {CURLE_SSL_SHUTDOWN_FAILED, ResultCode::SSL_CONNECTION_ERROR},
          {CURLE_USE_SSL_FAILED, ResultCode::SSL_CONNECTION_ERROR},

          // Certificate/Verification Errors
          {CURLE_PEER_FAILED_VERIFICATION, ResultCode::PEER_CERTIFICATE_ERROR},
          {CURLE_SSL_CERTPROBLEM, ResultCode::PEER_CERTIFICATE_ERROR},
          {CURLE_SSL_CACERT_BADFILE, ResultCode::PEER_CERTIFICATE_ERROR},
          {CURLE_SSL_CRL_BADFILE, ResultCode::PEER_CERTIFICATE_ERROR},
          {CURLE_SSL_ISSUER_ERROR, ResultCode::PEER_CERTIFICATE_ERROR},
          {CURLE_SSL_PINNEDPUBKEYNOTMATCH, ResultCode::PEER_CERTIFICATE_ERROR},
          {CURLE_SSL_INVALIDCERTSTATUS, ResultCode::PEER_CERTIFICATE_ERROR},
          {CURLE_SSL_CLIENTCERT, ResultCode::PEER_CERTIFICATE_ERROR},

          // HTTP Response Errors
          {CURLE_HTTP_RETURNED_ERROR, ResultCode::HTTP_ERROR},
          {CURLE_HTTP2, ResultCode::HTTP_ERROR},
          {CURLE_HTTP2_STREAM, ResultCode::HTTP_ERROR},
          {CURLE_HTTP3, ResultCode::HTTP_ERROR},
          {CURLE_WEIRD_SERVER_REPLY, ResultCode::HTTP_ERROR},
          {CURLE_RANGE_ERROR, ResultCode::HTTP_ERROR},
          {CURLE_HTTP_POST_ERROR, ResultCode::HTTP_ERROR},
          {CURLE_TOO_MANY_REDIRECTS, ResultCode::HTTP_ERROR},
          {CURLE_BAD_CONTENT_ENCODING, ResultCode::HTTP_ERROR},

          // Network Interface Errors
          {CURLE_INTERFACE_FAILED, ResultCode::NO_NETWORK_ERROR},
          {CURLE_NO_CONNECTION_AVAILABLE, ResultCode::NO_NETWORK_ERROR},

          // Internal Errors
          {CURLE_FAILED_INIT, ResultCode::INTERNAL_ERROR},
          {CURLE_OUT_OF_MEMORY, ResultCode::INTERNAL_ERROR},
          {CURLE_BAD_FUNCTION_ARGUMENT, ResultCode::INTERNAL_ERROR},
          {CURLE_UNKNOWN_OPTION, ResultCode::INTERNAL_ERROR},
          {CURLE_NOT_BUILT_IN, ResultCode::INTERNAL_ERROR},
      });

  const auto* value = base::FindOrNull(kCurlErrorMap, curl_result);
  return value ? *value : ResultCode::UNKNOWN_ERROR;
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
