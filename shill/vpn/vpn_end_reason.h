// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_END_REASON_H_
#define SHILL_VPN_VPN_END_REASON_H_

#include <string_view>

#include "shill/service.h"

namespace shill {

// Describes why a VPN connection ended (from connecting or connected to idle).
enum class VPNEndReason {
  // The disconnection is triggered from the upper layer, e.g., initiated from
  // user, or there is another VPN being connected and thus the current one is
  // disconnected.
  kDisconnectRequest,

  // The connection is lost due to the underlying physical network change.
  kNetworkChange,

  // Authentication failures. It might be arguable that whether we need to
  // separate them into different types, but currently difference message will
  // shown in the UI.
  kConnectFailureAuthPPP,
  kConnectFailureAuthCert,
  kConnectFailureAuthUserPassword,

  // Cannot resolve the VPN server name.
  kConnectFailureDNSLookup,

  // Failed to establish the VPN connection in the given time.
  kConnectTimeout,

  // The configuration for this VPN service is invalid.
  kInvalidConfig,

  // Something went wrong unexpectedly, e.g., bad state on the system.
  kFailureInternal,

  // Other failures cannot be categorized into above categories. This can be
  // either expected (e.g., VPN server is not reachable) or unexpected (e.g.,
  // some issue in the VPN executables but we couldn't get the reason from
  // it). We want to reduce the occurrence of this as much as possible.
  kFailureUnknown,
};

// Maps EndReasons to Service::ConnectFailures.
Service::ConnectFailure VPNEndReasonToServiceFailure(VPNEndReason reason);

// Maps EndReasons to strings.
std::string_view VPNEndReasonToString(VPNEndReason);

}  // namespace shill

#endif  // SHILL_VPN_VPN_END_REASON_H_
