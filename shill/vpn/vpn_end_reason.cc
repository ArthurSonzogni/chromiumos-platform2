// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_end_reason.h"

#include "shill/service.h"

namespace shill {

Service::ConnectFailure VPNEndReasonToServiceFailure(VPNEndReason reason) {
  switch (reason) {
    case VPNEndReason::kDisconnectRequest:
      return Service::kFailureDisconnect;
    case VPNEndReason::kNetworkChange:
      return Service::kFailureConnect;
    case VPNEndReason::kConnectFailureAuthPPP:
      return Service::kFailurePPPAuth;
    case VPNEndReason::kConnectFailureAuthCert:
      // This will be shown as "Authentication certificate rejected by network"
      // in UI.
      return Service::kFailureIPsecCertAuth;
    case VPNEndReason::kConnectFailureAuthUserPassword:
      // This will be shown as "Username/password incorrect or EAP-auth failed"
      // in UI.
      return Service::kFailureEAPAuthentication;
    case VPNEndReason::kConnectFailureDNSLookup:
      return Service::kFailureDNSLookup;
    case VPNEndReason::kConnectTimeout:
      return Service::kFailureConnect;
    case VPNEndReason::kInvalidConfig:
      return Service::kFailureConnect;
    case VPNEndReason::kFailureInternal:
      return Service::kFailureInternal;
    case VPNEndReason::kFailureUnknown:
      return Service::kFailureConnect;
  }
}

}  // namespace shill
