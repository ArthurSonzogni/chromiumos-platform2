// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/containers/fixed_flat_map.h>
#include <base/containers/map_util.h>
#include <base/notreached.h>
#include <brillo/http/http_transport_error.h>

namespace brillo::http {

namespace {

constexpr auto kTransportErrorNames =
    base::MakeFixedFlatMap<TransportError, std::string_view>({
        {TransportError::kUnknown, "kUnknown"},
        {TransportError::kDnsFailure, "kDnsFailure"},
        {TransportError::kDnsTimeout, "kDnsTimeout"},
        {TransportError::kProxyDnsFailure, "kProxyDnsFailure"},
        {TransportError::kProxyConnectionFailure, "kProxyConnectionFailure"},
        {TransportError::kConnectionFailure, "kConnectionFailure"},
        {TransportError::kTimeout, "kTimeout"},
        {TransportError::kTLSFailure, "kTLSFailure"},
        {TransportError::kCertificateError, "kCertificateError"},
        {TransportError::kHttpError, "kHttpError"},
        {TransportError::kIOError, "kIOError"},
        {TransportError::kNetworkError, "kNetworkError"},
        {TransportError::kInternalError, "kInternalError"},
        {TransportError::kBackendFailed, "kBackendFailed"},
    });

std::optional<TransportError> StringToTransportError(std::string_view code) {
  for (const auto& [error, name] : kTransportErrorNames) {
    if (name == code) {
      return error;
    }
  }
  return std::nullopt;
}

}  // namespace

std::string_view TransportErrorToString(TransportError code) {
  const auto* name = base::FindOrNull(kTransportErrorNames, code);
  CHECK(name);
  return *name;
}

std::optional<TransportError> ClassifyTransportError(
    const brillo::Error* error) {
  const auto* entry =
      brillo::Error::FindErrorOfDomain(error, kTransportErrorDomain);
  if (!entry) {
    return std::nullopt;
  }
  return StringToTransportError(entry->GetCode());
}

void AddTransportError(brillo::ErrorPtr* error,
                       const base::Location& location,
                       TransportError code,
                       std::string_view message) {
  brillo::Error::AddTo(error, location, kTransportErrorDomain,
                       TransportErrorToString(code), message);
}

}  // namespace brillo::http
