// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_ROUTING_POLICY_ENTRY_H_
#define ROUTING_SIMULATOR_ROUTING_POLICY_ENTRY_H_

#include <string>

#include <net-base/ip_address.h>

#include "routing-simulator/packet.h"

namespace routing_simulator {

// Represents a routing policy entry in the routing policy table.
class RoutingPolicyEntry {
 public:
  // Creates a RoutingPolicyEntry object from the string form of a policy entry
  // in ip rule. For example, if the input is "1010: from all oif eth0 lookup
  // 1002", creates a RoutingPolicyEntry object members of which are
  //  [priority_: 1010, source_prefix_: 0.0.0.0/0, table_id_: "1002"
  //  output_interface_: "eth0", input_interface_: "", fwmark_: ""].
  // For |output_interface|, |input_interface| and |fwmark|, an empty string is
  // set, if the input does not contain a value. Returns std::nullopt if the
  // format is invalid.
  static std::optional<RoutingPolicyEntry> CreateFromPolicyString(
      std::string_view policy_string, net_base::IPFamily ip_family);

  // RoutingPolicyEntry is only copyable.
  RoutingPolicyEntry(const RoutingPolicyEntry& other);
  RoutingPolicyEntry& operator=(const RoutingPolicyEntry& other);

  // Getter methods for the internal data.
  int priority() const { return priority_; }
  const net_base::IPCIDR& source_prefix() const { return source_prefix_; }
  const std::string& table_id() const { return table_id_; }
  const std::string& output_interface() const { return output_interface_; }
  const std::string& input_interface() const { return input_interface_; }
  const std::string& fwmark() const { return fwmark_; }

  bool operator==(const RoutingPolicyEntry& rhs) const;

  // Checks if a policy matches the input packet and returns true if it does.
  bool IsMatch(const Packet& packet) const;

 private:
  explicit RoutingPolicyEntry(net_base::IPFamily ip_family);

  // Parses a priority part in a policy entry in a routing policy table and sets
  // |priority_| to the value. Returns false if the parsing failed.
  bool SetPriority(std::string_view priority_string);

  // Parses a policy entry in a routing policy table and sets
  // |source_prefix_| to the value. Returns false if a parse with a source
  // prefix failed.
  bool SetSourcePrefix(base::span<const std::string_view>* policy_tokens_span,
                       net_base::IPFamily ip_family);

  // Parses a policy entry and sets items to the members of a RoutingPolicyEntry
  // object such as |output_interface_|, |input_interface_|, |fwmark_| and
  // |table_id_|. Returns false if the parsing failed.
  bool SetItems(base::span<const std::string_view> policy_tokens_span);

  int priority_ = 0;
  net_base::IPCIDR source_prefix_;
  std::string table_id_;
  std::string output_interface_;
  std::string input_interface_;
  // TODO(b/307460180): Change the type of fwmark to a struct that has 2
  // members; mark and mask.
  std::string fwmark_;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_ROUTING_POLICY_ENTRY_H_
