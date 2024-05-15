// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_end_reason.h"

#include <string_view>

namespace shill {

std::string_view VPNEndReasonToString(VPNEndReason reason) {
  switch (reason) {
    case VPNEndReason::kDisconnectRequest:
      return "disconnect";
    case VPNEndReason::kNetworkChange:
      return "network_change";
    case VPNEndReason::kConnectFailureAuthPPP:
      return "connect_failure_auth_ppp";
    case VPNEndReason::kConnectFailureAuthCert:
      return "connect_failure_auth_cert";
    case VPNEndReason::kConnectFailureAuthUserPassword:
      return "connect_failure_auth_user_password";
    case VPNEndReason::kConnectFailureDNSLookup:
      return "connect_failure_dns_lookup";
    case VPNEndReason::kConnectTimeout:
      return "connect_failure_timeout";
    case VPNEndReason::kInvalidConfig:
      return "connect_failure_invalid_config";
    case VPNEndReason::kFailureInternal:
      return "connect_failure_internal";
    case VPNEndReason::kFailureUnknown:
      return "connect_failure_unknown";
  }
}

}  // namespace shill
