// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_HTTP_HTTP_TRANSPORT_ERROR_H_
#define LIBBRILLO_BRILLO_HTTP_HTTP_TRANSPORT_ERROR_H_

#include <optional>
#include <string_view>

#include <base/location.h>
#include <brillo/brillo_export.h>
#include <brillo/errors/error.h>

namespace brillo::http {

// Error domain for transport-level error classification.
inline constexpr char kTransportErrorDomain[] = "transport_error";

// Implementation-agnostic HTTP transport error classification.
enum class TransportError {
  // Unmapped or unrecognized transport error.
  kUnknown,
  // Host name resolution failed.
  kDnsFailure,
  // Host name resolution timed out.
  kDnsTimeout,
  // Proxy name resolution failed.
  kProxyDnsFailure,
  // Could not connect to the proxy server.
  kProxyConnectionFailure,
  // Could not connect to the remote host.
  kConnectionFailure,
  // The operation timed out.
  kTimeout,
  // TLS/SSL handshake or engine failure.
  kTLSFailure,
  // Certificate verification failed.
  kCertificateError,
  // HTTP protocol error (bad response, HTTP/2 stream error, etc.).
  kHttpError,
  // Local read/write error during transfer.
  kIOError,
  // Network interface or routing error.
  kNetworkError,
  // Internal library error (init failure, OOM, bad argument, etc.).
  kInternalError,
  // The HTTP transport backend could not be initialized.
  kBackendFailed,
};

// Returns a human-readable name for `code` (e.g. "kTimeout").
BRILLO_EXPORT std::string_view TransportErrorToString(TransportError code);

// Extracts a TransportError from `error` by searching the chain for
// kTransportErrorDomain and matching the code string against known
// enum names. Returns std::nullopt if `error` is null or has no
// kTransportErrorDomain entry.
BRILLO_EXPORT std::optional<TransportError> ClassifyTransportError(
    const brillo::Error* error);

// Appends a brillo::Error with domain kTransportErrorDomain and
// TransportErrorToString(`code`) as the code string onto `error`.
BRILLO_EXPORT void AddTransportError(brillo::ErrorPtr* error,
                                     const base::Location& location,
                                     TransportError code,
                                     std::string_view message);

}  // namespace brillo::http

#endif  // LIBBRILLO_BRILLO_HTTP_HTTP_TRANSPORT_ERROR_H_
