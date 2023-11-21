// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/route.h"

#include <optional>
#include <string_view>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <base/containers/span.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <net-base/ip_address.h>
#include <net-base/ip_address_utils.h>

namespace routing_simulator {
namespace {

// Sets the value to the |destination_prefix| and returns true if the format of
// the prefix in the string form of a route entry in ip route is valid. If the
// format is invalid, returns false. For example, if an input string is
// "192.25.25.0/24 dev eth0" as input, sets |destination_prefix| to
// "192.25.25.0/24". Another example is "default via 100.87.84.254 dev eth0
// table 1002 metric "65536", sets |destination_prefix| to "0.0.0.0/0".
bool GetPrefix(const std::string_view prefix_token,
               net_base::IPFamily ip_family,
               net_base::IPCIDR* destination_prefix) {
  if (prefix_token == "default") {
    *destination_prefix = net_base::IPCIDR(ip_family);
    return true;
  }
  const auto prefix = net_base::IPCIDR::CreateFromCIDRString(prefix_token);
  if (!prefix) {
    return false;
  }
  *destination_prefix = *prefix;
  return true;
}

// Sets the value to |table_id| and returns true if the format of the table id
// in the string form of a route entry in ip route is valid. For example, if an
// input string is "default via 100.86.211.254 dev wlan0 table 1003 metric
// 65536" sets |table_id| to "1003". If there is no "table" identifier, set the
// default table id "main". If the format is invalid, returns false.
bool GetTableId(const std::vector<std::string_view>& route_tokens,
                std::string* table_id) {
  static constexpr std::string_view kTableIdIdentifier = "table";
  const auto it =
      std::find(route_tokens.begin(), route_tokens.end(), kTableIdIdentifier);
  if (it == route_tokens.end()) {
    *table_id = "main";
    return true;
  }
  if (it + 1 == route_tokens.end()) {
    return false;
  }
  *table_id = std::string(*(it + 1));
  return true;
}

// Sets the value to |next_hop| and returns true if the format of the next hop
// in the string form of a route entry in ip route is valid. For example, if an
// input string is "default via 100.86.211.254 dev wlan0 table 1003 metric
// 65536", sets |next_hop| to "100.86.211.254". If the format is invalid,
// returns false.
bool GetNextHop(const std::vector<std::string_view>& route_tokens,
                std::optional<net_base::IPAddress>* next_hop) {
  static constexpr std::string_view kNextHopIdentifier = "via";
  const auto it =
      std::find(route_tokens.begin(), route_tokens.end(), kNextHopIdentifier);
  if (it == route_tokens.end()) {
    *next_hop = std::nullopt;
    return true;
  }
  if (it + 1 == route_tokens.end()) {
    return false;
  }
  const auto prefix = net_base::IPAddress::CreateFromString(*(it + 1));
  if (!prefix) {
    return false;
  }
  *next_hop = prefix;
  return true;
}

// Sets the value to |output_interface| and returns true if the format of the
// output interface in the string form of a route entry in ip route is valid .
// For example, if an input string is "192.25.25.0/24 dev eth0" sets
// |output_interface| to "eth0". If the format is invalid, returns false.
bool GetOutputInterface(const std::vector<std::string_view>& route_tokens,
                        std::string* output_interface) {
  static constexpr std::string_view kOutputInterfaceIdentifier = "dev";
  const auto it = std::find(route_tokens.begin(), route_tokens.end(),
                            kOutputInterfaceIdentifier);
  if (it == route_tokens.end()) {
    *output_interface = "";
    return true;
  }
  if (it + 1 == route_tokens.end()) {
    return false;
  }
  *output_interface = *(it + 1);
  return true;
}

// Returns the route type in the string form of a route entry in ip route.
// Parses the route type string. Returns std::nullopt is the input string is not
// a valid route type.
std::optional<Route::Type> ParseRouteType(std::string_view type) {
  static constexpr auto kStrToRouteType =
      base::MakeFixedFlatMap<std::string_view, Route::Type>({
          {"unicast", Route::Type::kUnicast},
          {"broadcast", Route::Type::kBroadcast},
          {"anycast", Route::Type::kAnycast},
          {"local", Route::Type::kLocal},
          {"blackhole", Route::Type::kBlackhole},
          {"unreachable", Route::Type::kUnreachable},
          {"prohibit", Route::Type::kProhibit},
          {"throw", Route::Type::kThrow},
          {"multicast", Route::Type::kMulticast},
      });
  const auto it = kStrToRouteType.find(type);
  if (it == kStrToRouteType.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace

// static
// TODO(b/307460180): Refactor the code using for loop for each token and
// combine helper functions for each identifier into one function. In addition
// to that, add a check if some identifiers exist adjacently.
// (e.g. "192.25.25.0/24 dev table main").
std::optional<Route> Route::CreateFromRouteString(
    std::string_view route_string, net_base::IPFamily ip_family) {
  const auto route_tokens = base::SplitStringPiece(
      route_string, " \t", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  auto type = Type::kUnicast;

  // Check if the first component of the route_tokens is the route type or
  // not.
  if (route_tokens.size() == 0) {
    LOG(ERROR) << "No input";
    return std::nullopt;
  }
  base::span<const std::string_view> route_tokens_span = route_tokens;
  const auto route_type = ParseRouteType(route_tokens_span[0]);
  if (route_type) {
    route_tokens_span = route_tokens_span.subspan(1);
    type = route_type.value();
  }
  Route route(type, ip_family);

  if (route_tokens_span.size() == 0) {
    LOG(ERROR) << "There is only route type in " << route_string;
    return std::nullopt;
  }
  // Check if a route_tokens includes necessary components.
  if (!GetPrefix(route_tokens_span[0], ip_family, &route.destination_prefix_)) {
    LOG(ERROR) << "Failed to parse prefix in " << route_string;
    return std::nullopt;
  }
  if (!GetTableId(route_tokens, &route.table_id_)) {
    LOG(ERROR) << "Failed to parse table id in " << route_string;
    return std::nullopt;
  }

  // Set the value to the corresponding field of a optional member.
  if (!GetOutputInterface(route_tokens, &route.output_interface_)) {
    LOG(ERROR) << "Failed to parse output interface in " << route_string;
    return std::nullopt;
  }
  if (!GetNextHop(route_tokens, &route.next_hop_)) {
    LOG(ERROR) << "Failed to parse next hop in " << route_string;
    return std::nullopt;
  }
  route.route_str_ = route_string;
  return route;
}

Route::Route(const Route& other) = default;
Route& Route::operator=(const Route& other) = default;

bool Route::operator==(const Route& rhs) const = default;

Route::Route(Type type, net_base::IPFamily ip_family)
    : destination_prefix_(ip_family), type_(type) {}

}  // namespace routing_simulator
