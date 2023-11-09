// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/routing_policy_entry.h"

#include <map>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>
#include <net-base/ip_address.h>

namespace routing_simulator {
namespace {

// Parse an input string to Fwmark and returns it if the parsing is successful.
// Otherwise, returns std::nullopt.
std::optional<RoutingPolicyEntry::Fwmark> ParseFmwarkWithMask(
    std::string_view fwmark_str) {
  RoutingPolicyEntry::Fwmark fwmark;
  const auto fwmark_tokens = base::SplitStringPiece(
      fwmark_str, "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (fwmark_tokens.size() != 2) {
    return std::nullopt;
  }
  if ((!base::HexStringToUInt(fwmark_tokens[0], &fwmark.mark)) ||
      (!base::HexStringToUInt(fwmark_tokens[1], &fwmark.mask))) {
    return std::nullopt;
  }
  return fwmark;
}

}  // namespace

bool RoutingPolicyEntry::Fwmark::operator==(const Fwmark& rhs) const {
  return ((mark == rhs.mark) && (mask == rhs.mask));
}

// static
std::optional<RoutingPolicyEntry> RoutingPolicyEntry::CreateFromPolicyString(
    std::string_view policy_string, net_base::IPFamily ip_family) {
  RoutingPolicyEntry policy(ip_family);
  const auto policy_tokens = base::SplitStringPiece(
      policy_string, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  base::span<const std::string_view> policy_tokens_span = policy_tokens;
  // Check if the first component of |policy_tokens| is a priority.
  if (policy_tokens.empty()) {
    LOG(ERROR) << "Input is empty";
    return std::nullopt;
  }
  if (!policy.SetPriority(policy_tokens_span[0])) {
    LOG(ERROR) << "Parse with priority fails in: " << policy_string;
    return std::nullopt;
  }
  policy_tokens_span = policy_tokens_span.subspan(1);
  if (policy_tokens_span.empty()) {
    LOG(ERROR) << "There is no string after priority in: " << policy_string;
    return std::nullopt;
  }
  // Check if the second component of |policy_tokens| is a source prefix
  // identifier.
  if (!policy.SetSourcePrefix(&policy_tokens_span, ip_family)) {
    LOG(ERROR) << "Parse with source prefix fails in: " << policy_string;
    return std::nullopt;
  }
  // Parse other components in |policy_tokens_span| and set the value to the
  // corresponding member field of a RoutingPolicyEntry object.
  if (!policy.SetItems(policy_tokens_span)) {
    LOG(ERROR) << "Input strings is not valid: " << policy_string;
    return std::nullopt;
  }
  if (policy.table_id_ == "") {
    LOG(ERROR) << "There is no table id in: " << policy_string;
    return std::nullopt;
  }
  policy.policy_str_ = policy_string;
  return policy;
}

RoutingPolicyEntry::RoutingPolicyEntry(const RoutingPolicyEntry& other) =
    default;
RoutingPolicyEntry& RoutingPolicyEntry::operator=(
    const RoutingPolicyEntry& other) = default;

bool RoutingPolicyEntry::operator==(const RoutingPolicyEntry& rhs) const =
    default;

// TODO(b/307460180): Change the interface (output/input parameter or returned
// value type) to save the matched policy to the result.
bool RoutingPolicyEntry::Matches(const Packet& packet) const {
  if (!source_prefix().InSameSubnetWith(packet.source_ip())) {
    return false;
  }
  if (!output_interface().empty()) {
    if (packet.output_interface() != output_interface()) {
      return false;
    }
  }
  if (!input_interface().empty()) {
    if (packet.input_interface() != input_interface()) {
      return false;
    }
  }
  if ((packet.fwmark() & fwmark().mask) != (fwmark().mark & fwmark().mask)) {
    return false;
  }
  return true;
}

RoutingPolicyEntry::RoutingPolicyEntry(net_base::IPFamily ip_family)
    : source_prefix_(net_base::IPCIDR(ip_family)) {}

bool RoutingPolicyEntry::SetPriority(std::string_view priority_string) {
  const auto priority_token = base::SplitStringPiece(
      priority_string, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  // IF a priority token is "1001:", |priority_token| should be like {1001} and
  // thus the size of it should be 1.
  if (priority_token.size() != 1) {
    return false;
  }
  int priority;
  if (!base::StringToInt(priority_token[0], &priority)) {
    LOG(ERROR) << "Formats of priority is invalid in: " << priority_token[0];
    return false;
  }
  // |priority| should be the number from 0 to 32767.
  if (priority < 0 || priority > 32767) {
    LOG(ERROR) << "Priority is out of range (0<=priority<=32767)";
    return false;
  }
  priority_ = priority;
  return true;
}

bool RoutingPolicyEntry::SetSourcePrefix(
    base::span<const std::string_view>* policy_tokens_span_ptr,
    net_base::IPFamily ip_family) {
  static constexpr std::string_view kSourcePrefixIdentifier = "from";
  if (policy_tokens_span_ptr->front() != kSourcePrefixIdentifier) {
    LOG(ERROR) << "There is no source prefix identifier";
    return false;
  }
  *policy_tokens_span_ptr = policy_tokens_span_ptr->subspan(1);
  if (policy_tokens_span_ptr->empty()) {
    LOG(ERROR) << "No source prefix found after the identifier (from)";
    return false;
  }
  if (policy_tokens_span_ptr->front() == "all") {
    source_prefix_ = net_base::IPCIDR(ip_family);
    *policy_tokens_span_ptr = policy_tokens_span_ptr->subspan(1);
    return true;
  }
  const auto source_prefix =
      net_base::IPCIDR::CreateFromCIDRString(policy_tokens_span_ptr->front());
  if (!source_prefix) {
    LOG(ERROR) << "Formats of source prefix is invalid in: "
               << policy_tokens_span_ptr->front();
    return false;
  }
  source_prefix_ = *source_prefix;
  *policy_tokens_span_ptr = policy_tokens_span_ptr->subspan(1);
  return true;
}

bool RoutingPolicyEntry::SetItems(
    base::span<const std::string_view> policy_tokens_span) {
  static constexpr std::string_view kOutputInterfaceIdentifier = "oif";
  static constexpr std::string_view kInputInterfaceIdentifier = "iif";
  static constexpr std::string_view kFwmarkIdentifier = "fwmark";
  static constexpr std::string_view kTableIdIdentifier = "lookup";
  std::string fwmark_str;
  // map: {[key]:identifier, [value]: {member name, a pointer to the member}}
  // We need a "member name" only for logging.
  const std::map<std::string_view, std::pair<std::string_view, std::string*>>
      identifier_map = {
          {kOutputInterfaceIdentifier,
           {"output interface", &output_interface_}},
          {kInputInterfaceIdentifier, {"input interface", &input_interface_}},
          {kFwmarkIdentifier, {"fwmark", &fwmark_str}},
          {kTableIdIdentifier, {"table id", &table_id_}}};
  while (!policy_tokens_span.empty()) {
    if (auto it = identifier_map.find(policy_tokens_span.front());
        it != identifier_map.end()) {
      const auto& identifer = it->first;
      const auto& name = it->second.first;
      auto* member_to_set_ptr = it->second.second;
      policy_tokens_span = policy_tokens_span.subspan(1);
      if (policy_tokens_span.empty()) {
        LOG(ERROR) << "No " << name << " found after the identifier ["
                   << identifer << " ]";
        return false;
      }
      if (identifier_map.contains(policy_tokens_span.front())) {
        LOG(ERROR) << "Identifiers are next to each other";
        return false;
      }
      *member_to_set_ptr = policy_tokens_span.front();
      if (name == kFwmarkIdentifier) {
        const auto fwmark = ParseFmwarkWithMask(fwmark_str);
        if (!fwmark) {
          LOG(ERROR) << "Formats of fwmark is invalid in: " << fwmark_str;
          return false;
        }
        fwmark_ = *fwmark;
      }
      policy_tokens_span = policy_tokens_span.subspan(1);
    } else {
      // There is a token without the corresponding identifier before it.
      LOG(ERROR) << "Unknown token: " << policy_tokens_span.front();
      return false;
    }
  }
  return true;
}

}  // namespace routing_simulator
