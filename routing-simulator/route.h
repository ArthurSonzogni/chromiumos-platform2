// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_ROUTE_H_
#define ROUTING_SIMULATOR_ROUTE_H_

#include <string>
#include <string_view>

#include <net-base/ip_address.h>
#include <net-base/ip_address_utils.h>

namespace routing_simulator {

// Represents a route entry that consists of a destination prefix in CIDR format
// and an output interface.
class Route {
 public:
  // Takes route-notation strings like '192.25.25.0/24 dev eth0' as input.
  static std::optional<Route> CreateFromRouteString(
      std::string_view route_string);

  // Route is only copyable.
  Route(const Route& other);
  Route& operator=(const Route& other);

  // Getter methods for the internal data.
  net_base::IPCIDR prefix() const { return prefix_; }
  std::string output_interface() const { return output_interface_; }

  bool operator==(const Route& rhs) const;

 private:
  Route(net_base::IPCIDR prefix, std::string_view output_interface);

  net_base::IPCIDR prefix_;
  std::string output_interface_;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_ROUTE_H_
