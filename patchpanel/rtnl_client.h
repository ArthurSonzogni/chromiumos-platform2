// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_RTNL_CLIENT_H_
#define PATCHPANEL_RTNL_CLIENT_H_

#include <map>
#include <memory>

#include <base/files/scoped_file.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/mac_address.h>

namespace patchpanel {

// The client of RTNETLINK linux API.
class RTNLClient {
 public:
  static std::unique_ptr<RTNLClient> Create();

  virtual ~RTNLClient();

  // Queries the MAC address of IPv4 or IPv6 neighbors. Returns the mapping from
  // the IP address to the MAC address.
  // If |ifindex| is not nullopt, then only returns the neighbors for this
  // network interface. The value of the |ifindex| could be retrieved by
  // System::IfNametoindex().
  virtual std::map<net_base::IPv4Address, net_base::MacAddress>
  GetIPv4NeighborMacTable(
      const std::optional<int>& ifindex = std::nullopt) const;
  virtual std::map<net_base::IPv6Address, net_base::MacAddress>
  GetIPv6NeighborMacTable(
      const std::optional<int>& ifindex = std::nullopt) const;

 protected:
  explicit RTNLClient(base::ScopedFD rtnl_fd);

 private:
  base::ScopedFD rtnl_fd_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_RTNL_CLIENT_H_
