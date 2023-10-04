// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/route.h"

#include <string_view>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <net-base/ip_address_utils.h>

namespace routing_simulator {
namespace {
constexpr std::string_view kOutputInterfaceIdentifier = "dev";

// Returns the output interface in the string form of a route entry in ip route.
// For example, "192.25.25.0/24 dev eth0" as input and "eth0" as output.
std::optional<std::string> GetOutputInterface(
    const std::vector<std::string_view>& route_parts) {
  for (auto it = route_parts.begin(); it != route_parts.end(); it++) {
    if (*it == kOutputInterfaceIdentifier) {
      if (it + 1 == route_parts.end()) {
        return std::nullopt;
      }
      return std::string(*(it + 1));
    }
  }
  return std::nullopt;
}

// Splits the string form of a route entry in ip route into the pair of the CIDR
// and the output interface. For example, "192.25.25.0/24 dev eth0" as input
// and {"192.25.25.0/24", "eth0"} as output.
// Returns std::nullopt if the format is invalid.
std::optional<std::pair<net_base::IPCIDR, std::string>> SplitRouteString(
    const std::string_view route_string) {
  const auto route_parts = base::SplitStringPiece(
      route_string, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  // a route_parts should contain at least 3 components; prefix, dev and
  // output interface.
  if (route_parts.size() < 3) {
    LOG(ERROR) << "Input route string is not valid: " << route_string;
    return std::nullopt;
  }
  const auto prefix = net_base::IPCIDR::CreateFromCIDRString(route_parts[0]);
  if (!prefix) {
    LOG(ERROR) << "Input route string is not valid: " << route_string;
    return std::nullopt;
  }
  const auto output_interface = GetOutputInterface(route_parts);
  if (!output_interface) {
    return std::nullopt;
  }
  return std::make_pair(prefix.value(), output_interface.value());
}
}  // namespace

Route::Route(const Route& other) = default;
Route& Route::operator=(const Route& other) = default;

// static
std::optional<Route> Route::CreateFromRouteString(
    const std::string_view route_string) {
  const auto route_pair = SplitRouteString(route_string);
  if (!route_pair) {
    return std::nullopt;
  }
  return Route(route_pair.value().first, route_pair.value().second);
}

Route::Route(net_base::IPCIDR prefix, std::string_view output_interface)
    : prefix_(prefix), output_interface_(output_interface) {}

bool Route::operator==(const Route& rhs) const {
  return output_interface_ == rhs.output_interface_ && prefix_ == rhs.prefix_;
}

}  // namespace routing_simulator
