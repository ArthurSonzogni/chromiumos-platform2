// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_ROUTE_H_
#define ROUTING_SIMULATOR_ROUTE_H_

#include <optional>
#include <string>
#include <string_view>

#include <net-base/ip_address.h>
#include <net-base/ip_address_utils.h>

namespace routing_simulator {

// Represents a route entry that consists of a destination prefix in CIDR
// format, output interface, an IP address of the next hop (optional)
// and a route type.
class Route {
 public:
  enum class Type {
    kUnicast,
    kBroadcast,
    kAnycast,
    kLocal,
    kBlackhole,
    kUnreachable,
    kProhibit,
    kThrow,
    kMulticast,
  };

  // Creates a Route object from the string form of a route entry in ip.
  // For example, if the input is "local 100.115.92.133 dev arc_ns1 table
  // local proto kernel scope host src 100.115.92.133", create a route object
  // members of which are
  //  [type_: kLocal, destination_prefix_ 100.115.92.133,
  //  output_interface_: arc_ns1].
  // Returns std::nullopt if the format is invalid.
  static std::optional<Route> CreateFromRouteString(
      std::string_view route_string, net_base::IPFamily ip_family);

  // Route is copyable.
  Route(const Route& other);
  Route& operator=(const Route& other);

  // Getter methods for the internal data.
  const net_base::IPCIDR& destination_prefix() const {
    return destination_prefix_;
  }
  const std::string& output_interface() const { return output_interface_; }
  const std::optional<net_base::IPAddress>& next_hop() const {
    return next_hop_;
  }
  Type type() const { return type_; }
  const std::string& table_id() const { return table_id_; }
  const std::string& route_str() const { return route_str_; }

  bool operator==(const Route& rhs) const;

 private:
  Route(Type type, net_base::IPFamily ip_family);

  net_base::IPCIDR destination_prefix_;
  std::string output_interface_;
  std::optional<net_base::IPAddress> next_hop_;
  Type type_;
  std::string table_id_;
  // The original string in ip route show to construct this object.
  std::string route_str_;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_ROUTE_H_
