// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_DHCP_PROVISION_REASONS_H_
#define SHILL_NETWORK_DHCP_PROVISION_REASONS_H_

#include <string>

namespace shill {

// Reason why DHCP provisioning starts.
enum class DHCPProvisionReason {
  // Normal connection.
  kConnect,
  // DHCP lease has expired. This only happens when dhcpcd has failed to renew
  // and rebind the lease.
  kLeaseExpiration,
  // WiFi roaming.
  kRoaming,
  // After resuming from suspend. Under most circumstances, WiFi disconnects
  // after suspend. For such cases, the reconnection after resuming is
  // regarded as kConnect.
  kSuspendResume,
};

// Maps DHCPProvisionReason from enums to strings for metric use.
std::string DHCPProvisionReasonToMetricString(DHCPProvisionReason reason);

}  // namespace shill
#endif  // SHILL_NETWORK_DHCP_PROVISION_REASONS_H_
