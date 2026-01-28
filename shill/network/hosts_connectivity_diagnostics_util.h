// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_UTIL_H_
#define SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_UTIL_H_

#include <curl/curl.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

namespace shill {

// Maps CURLcode to ConnectivityResultCode for connectivity diagnostics.
// Returns `ResultCode::UNKNOWN_ERROR` if invalid or unknown CURLcode is
// given.
hosts_connectivity_diagnostics::ConnectivityResultCode
CurlErrorToConnectivityResultCode(CURLcode curl_result);

}  // namespace shill

#endif  // SHILL_NETWORK_HOSTS_CONNECTIVITY_DIAGNOSTICS_UTIL_H_
