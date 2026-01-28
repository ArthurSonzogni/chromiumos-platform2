// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics_util.h"

#include <base/containers/fixed_flat_map.h>
#include <base/containers/map_util.h>

namespace shill {

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

}  // namespace shill
