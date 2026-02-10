// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics_util.h"

#include <array>
#include <string>

#include <curl/curl.h>
#include <gtest/gtest.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

namespace shill {
namespace {

using ResultCode = hosts_connectivity_diagnostics::ConnectivityResultCode;

TEST(HostsConnectivityDiagnosticsUtilTest, CurlErrorMapping) {
  struct TestCase {
    CURLcode curl_code;
    ResultCode expected;
  };

  const std::array kTestCases = {
      // Success
      TestCase{CURLE_OK, ResultCode::SUCCESS},

      // DNS errors
      TestCase{CURLE_COULDNT_RESOLVE_HOST, ResultCode::DNS_RESOLUTION_ERROR},
      TestCase{CURLE_COULDNT_RESOLVE_PROXY,
               ResultCode::PROXY_DNS_RESOLUTION_ERROR},

      // Proxy errors
      TestCase{CURLE_PROXY, ResultCode::PROXY_CONNECTION_FAILURE},

      // Connection failures
      TestCase{CURLE_COULDNT_CONNECT, ResultCode::CONNECTION_FAILURE},
      TestCase{CURLE_GOT_NOTHING, ResultCode::CONNECTION_FAILURE},

      // Timeout
      TestCase{CURLE_OPERATION_TIMEDOUT, ResultCode::CONNECTION_TIMEOUT},

      // SSL/TLS errors
      TestCase{CURLE_SSL_CONNECT_ERROR, ResultCode::SSL_CONNECTION_ERROR},
      TestCase{CURLE_SSL_ENGINE_NOTFOUND, ResultCode::SSL_CONNECTION_ERROR},
      TestCase{CURLE_SSL_ENGINE_SETFAILED, ResultCode::SSL_CONNECTION_ERROR},
      TestCase{CURLE_SSL_ENGINE_INITFAILED, ResultCode::SSL_CONNECTION_ERROR},
      TestCase{CURLE_SSL_CIPHER, ResultCode::SSL_CONNECTION_ERROR},
      TestCase{CURLE_SSL_SHUTDOWN_FAILED, ResultCode::SSL_CONNECTION_ERROR},
      TestCase{CURLE_USE_SSL_FAILED, ResultCode::SSL_CONNECTION_ERROR},

      // Certificate errors
      TestCase{CURLE_PEER_FAILED_VERIFICATION,
               ResultCode::PEER_CERTIFICATE_ERROR},
      TestCase{CURLE_SSL_CERTPROBLEM, ResultCode::PEER_CERTIFICATE_ERROR},
      TestCase{CURLE_SSL_CACERT_BADFILE, ResultCode::PEER_CERTIFICATE_ERROR},
      TestCase{CURLE_SSL_CRL_BADFILE, ResultCode::PEER_CERTIFICATE_ERROR},
      TestCase{CURLE_SSL_ISSUER_ERROR, ResultCode::PEER_CERTIFICATE_ERROR},
      TestCase{CURLE_SSL_PINNEDPUBKEYNOTMATCH,
               ResultCode::PEER_CERTIFICATE_ERROR},
      TestCase{CURLE_SSL_INVALIDCERTSTATUS, ResultCode::PEER_CERTIFICATE_ERROR},
      TestCase{CURLE_SSL_CLIENTCERT, ResultCode::PEER_CERTIFICATE_ERROR},

      // HTTP errors
      TestCase{CURLE_HTTP_RETURNED_ERROR, ResultCode::HTTP_ERROR},
      TestCase{CURLE_HTTP2, ResultCode::HTTP_ERROR},
      TestCase{CURLE_HTTP2_STREAM, ResultCode::HTTP_ERROR},
      TestCase{CURLE_HTTP3, ResultCode::HTTP_ERROR},
      TestCase{CURLE_WEIRD_SERVER_REPLY, ResultCode::HTTP_ERROR},
      TestCase{CURLE_RANGE_ERROR, ResultCode::HTTP_ERROR},
      TestCase{CURLE_HTTP_POST_ERROR, ResultCode::HTTP_ERROR},
      TestCase{CURLE_TOO_MANY_REDIRECTS, ResultCode::HTTP_ERROR},
      TestCase{CURLE_BAD_CONTENT_ENCODING, ResultCode::HTTP_ERROR},

      // Network errors
      TestCase{CURLE_INTERFACE_FAILED, ResultCode::NO_NETWORK_ERROR},
      TestCase{CURLE_NO_CONNECTION_AVAILABLE, ResultCode::NO_NETWORK_ERROR},

      // Internal errors
      TestCase{CURLE_FAILED_INIT, ResultCode::INTERNAL_ERROR},
      TestCase{CURLE_OUT_OF_MEMORY, ResultCode::INTERNAL_ERROR},
      TestCase{CURLE_BAD_FUNCTION_ARGUMENT, ResultCode::INTERNAL_ERROR},
      TestCase{CURLE_UNKNOWN_OPTION, ResultCode::INTERNAL_ERROR},
      TestCase{CURLE_NOT_BUILT_IN, ResultCode::INTERNAL_ERROR},

      // Unknown errors (not in map)
      TestCase{CURLE_URL_MALFORMAT, ResultCode::UNKNOWN_ERROR},
      TestCase{CURLE_UNSUPPORTED_PROTOCOL, ResultCode::UNKNOWN_ERROR},
  };

  for (const auto& tc : kTestCases) {
    EXPECT_EQ(CurlErrorToConnectivityResultCode(tc.curl_code), tc.expected);
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
