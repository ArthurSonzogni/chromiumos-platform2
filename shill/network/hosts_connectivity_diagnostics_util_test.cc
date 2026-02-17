// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics_util.h"

#include <array>
#include <string>

#include <brillo/http/http_transport_error.h>
#include <gtest/gtest.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

namespace shill {
namespace {

using ResultCode = hosts_connectivity_diagnostics::ConnectivityResultCode;

TEST(HostsConnectivityDiagnosticsUtilTest, TransportErrorMapping) {
  using TE = brillo::http::TransportError;

  struct TestCase {
    TE error;
    ResultCode expected;
  };

  constexpr std::array kTestCases = {
      TestCase{TE::kDnsFailure, ResultCode::DNS_RESOLUTION_ERROR},
      TestCase{TE::kDnsTimeout, ResultCode::DNS_RESOLUTION_ERROR},
      TestCase{TE::kProxyDnsFailure, ResultCode::PROXY_DNS_RESOLUTION_ERROR},
      TestCase{TE::kProxyConnectionFailure,
               ResultCode::PROXY_CONNECTION_FAILURE},
      TestCase{TE::kConnectionFailure, ResultCode::CONNECTION_FAILURE},
      TestCase{TE::kTimeout, ResultCode::CONNECTION_TIMEOUT},
      TestCase{TE::kTLSFailure, ResultCode::SSL_CONNECTION_ERROR},
      TestCase{TE::kCertificateError, ResultCode::PEER_CERTIFICATE_ERROR},
      TestCase{TE::kHttpError, ResultCode::HTTP_ERROR},
      TestCase{TE::kNetworkError, ResultCode::NO_NETWORK_ERROR},
      TestCase{TE::kInternalError, ResultCode::INTERNAL_ERROR},
      TestCase{TE::kBackendFailed, ResultCode::INTERNAL_ERROR},
      TestCase{TE::kIOError, ResultCode::UNKNOWN_ERROR},
      TestCase{TE::kUnknown, ResultCode::UNKNOWN_ERROR},
  };

  for (const auto& tc : kTestCases) {
    SCOPED_TRACE(brillo::http::TransportErrorToString(tc.error));
    EXPECT_EQ(TransportErrorToConnectivityResultCode(tc.error), tc.expected);
  }
}

TEST(HostsConnectivityDiagnosticsUtilTest, IsValidProxyUrlAcceptsValid) {
  constexpr std::array<std::string_view, 16> kValidProxies = {
      "http://proxy.example.com:8080", "https://secure-proxy.example.com:443",
      "socks5://proxy.example.com:1080", "socks4://proxy.example.com:1080",
      "HTTP://PROXY.EXAMPLE.COM:8080",  // Case-insensitive scheme.
      "http://proxy.example.com",       // No port (uses default).
      // IPv6 addresses.
      "http://[::1]:8080",          // IPv6 localhost with port.
      "http://[2001:db8::1]:8080",  // IPv6 with port.
      "http://[::1]",               // IPv6 without port.
      "socks5://[::1]:1080",        // IPv6 socks5 proxy.
      // Userinfo (proxy authentication credentials).
      "http://user@proxy.example.com:8080",            // Username only.
      "http://user:pass@proxy.example.com:8080",       // Username and password.
      "socks5://user:pass@proxy.example.com:1080",     // SOCKS5 with auth.
      "http://user:p@ss:word@proxy.example.com:8080",  // Password with colon.
      "http://user:pass@[::1]:8080",                   // IPv6 with userinfo.
      "http://@proxy.example.com:8080",  // Empty userinfo is valid.
  };

  for (const auto& proxy : kValidProxies) {
    SCOPED_TRACE(proxy);
    EXPECT_TRUE(IsValidProxyUrl(std::string(proxy)));
  }
}

TEST(HostsConnectivityDiagnosticsUtilTest, IsValidProxyUrlRejectsInvalid) {
  constexpr std::array<std::string_view, 20> kInvalidProxies = {
      "ftp://proxy.example.com:21",      // Invalid scheme.
      "http://",                         // Missing host.
      "http:///",                        // Missing host with trailing slash.
      "socks://proxy.example.com:1080",  // Invalid socks scheme (not socks4/5).
      "invalid-proxy",                   // No scheme.
      "://proxy.example.com",            // Missing scheme.
      // Port validation.
      "http://proxy.com:abc",    // Non-numeric port.
      "http://proxy.com:0",      // Port 0 is invalid.
      "http://proxy.com:65536",  // Port > 65535.
      "http://proxy.com:-1",     // Negative port.
      // Path/query/fragment.
      "http://proxy.com:8080/path",  // Path is not allowed.
      "http://proxy.com?query",      // Query is not allowed.
      "http://proxy.com#fragment",   // Fragment is not allowed.
      // IPv6 malformed.
      "http://[",           // Unclosed bracket.
      "http://[]",          // Empty bracket.
      "http://[::1]:abc",   // IPv6 with non-numeric port.
      "http://[::1]extra",  // Extra chars after bracket without colon.
      // Ambiguous IPv6 without brackets.
      "http://::1:8080",  // Ambiguous - multiple colons without brackets.
      // Userinfo malformed.
      "http://user:pass@",       // Missing host after userinfo.
      "http://user:pass@:8080",  // Missing host, only port after userinfo.
  };

  for (const auto& proxy : kInvalidProxies) {
    SCOPED_TRACE(proxy);
    EXPECT_FALSE(IsValidProxyUrl(std::string(proxy)));
  }
}

}  // namespace
}  // namespace shill
