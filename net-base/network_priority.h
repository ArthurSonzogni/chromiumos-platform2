// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_PRIORITY_H_
#define NET_BASE_NETWORK_PRIORITY_H_

#include <cstdint>
#include <ostream>

#include <brillo/brillo_export.h>

namespace net_base {

// A representation of Manager SortServices() result that Network uses to apply
// its configuration accordingly.
// TODO(b/289971126): Migrate to patchpanel-client.
struct BRILLO_EXPORT NetworkPriority {
  static constexpr uint32_t kMaxRankingOrder = 31;
  // Whether current Network is the primary one. Is true for either VPN or the
  // primary physical network if a VPN network is not present.
  bool is_primary_logical = false;
  // Whether current Network is the highest-rank physical network.
  bool is_primary_physical = false;
  // Whether the DNS setting from current network should be set as system
  // default. Is true when all the networks with a higher rank do not have a
  // proper DNS configuration.
  bool is_primary_for_dns = false;
  // A unique value among networks specifying the ranking order of the networks.
  // Primary logical network has a value of 0, secondary network has a value of
  // 1, etc.
  uint32_t ranking_order = kMaxRankingOrder;

  bool operator==(const NetworkPriority& rhs) const = default;

  // Compares two priority objects in terms of routing (excluding
  // `is_primary_for_dns`).
  static bool HaveSameRoutingPriority(NetworkPriority a, NetworkPriority b) {
    return a.is_primary_logical == b.is_primary_logical &&
           a.is_primary_physical == b.is_primary_physical &&
           a.ranking_order == b.ranking_order;
  }
};

BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const NetworkPriority& priority);

}  // namespace net_base

#endif  // NET_BASE_NETWORK_PRIORITY_H_
