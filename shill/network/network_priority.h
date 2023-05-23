// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_PRIORITY_H_
#define SHILL_NETWORK_NETWORK_PRIORITY_H_

#include <cstdint>
#include <limits>
#include <ostream>

namespace shill {

// A representation of Manager SortServices() result that Network uses to apply
// its configuration accordingly.
struct NetworkPriority {
  // Whether current Network is the primary one. Is true for either VPN or the
  // primary physical network if a VPN network is not present.
  bool is_primary_logical = false;
  // Whether current Network is the highest-rank physical network.
  bool is_primary_physical = false;
  // Whether the DNS setting from current network should be set as system
  // default. Is true when all the networks with a higher rank do not have a
  // proper DNS configuration.
  bool is_primary_for_dns = false;
  // A unique priority value assigned by Manager according to the service order.
  // TODO(b/264963034): Use a generic value decoupled from routing rule table
  // implementation details.
  uint32_t priority_value = std::numeric_limits<uint32_t>::max() - 1;
};

bool operator==(const NetworkPriority& lhs, const NetworkPriority& rhs);
std::ostream& operator<<(std::ostream& stream, const NetworkPriority& priority);

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_PRIORITY_H_
