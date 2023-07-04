// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_counters_service.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <re2/re2.h>

#include "patchpanel/datapath.h"
#include "patchpanel/iptables.h"
#include "patchpanel/multicast_forwarder.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {
constexpr char kTechnologyWifi[] = "wifi";
constexpr char kTechnologyEthernet[] = "ethernet";
constexpr char kMdns[] = "mdns";
constexpr char kSsdp[] = "ssdp";

// Chain line is in the format of Chain rx_<protocol>
// For example: Chain rx_mdns
constexpr LazyRE2 kChainLine = {R"(Chain rx_(mdns|ssdp).*)"};
// Target name is in the format of rx_<technology>_<protocol>
// The target name for a defined source looks like: rx_ethernet_mdns,
// ex_wifi_ssdp
constexpr LazyRE2 kTargetName = {R"(rx_(ethernet|wifi)_(mdns|ssdp))"};
// Counter line is in the format of <packet> <byte> <target> <prot> <opt>
// <in> <out> <source> <destination> <option>
// The counter line for a defined source looks like:
// pkts   bytes  target    prot opt   in   out   source   destination
//   0      0 rx_wifi_mdns  all  --  wlan0  *  0.0.0.0/0   0.0.0.0/0
constexpr LazyRE2 kCounterLine = {R"( *(\d+) +\d+ +(\w+).*)"};

std::optional<MulticastCountersService::MulticastProtocolType>
StringToMulticastProtocolType(base::StringPiece protocol) {
  if (protocol == kMdns) {
    return MulticastCountersService::MulticastProtocolType::kMdns;
  }
  if (protocol == kSsdp) {
    return MulticastCountersService::MulticastProtocolType::kSsdp;
  }
  return std::nullopt;
}

std::optional<MulticastCountersService::MulticastTechnologyType>
StringToMulticastTechnologyType(base::StringPiece technology) {
  if (technology == kTechnologyEthernet) {
    return MulticastCountersService::MulticastTechnologyType::kEthernet;
  }
  if (technology == kTechnologyWifi) {
    return MulticastCountersService::MulticastTechnologyType::kWifi;
  }
  return std::nullopt;
}

std::optional<MulticastCountersService::CounterKey> GetCounterKeyFromTargetName(
    base::StringPiece target) {
  // Target is a part of counter line in iptables, and here the target names
  // are expected to be in the format of rx_<technology>_<protocol>. This
  // method will extract technology and protocol from target name.
  std::string technology;
  std::string protocol;
  if (!RE2::FullMatch(target.cbegin(), *kTargetName, &technology, &protocol)) {
    LOG(ERROR) << "Invalid target name: " << target;
    return std::nullopt;
  }

  MulticastCountersService::CounterKey key;
  if (!StringToMulticastProtocolType(protocol).has_value()) {
    LOG(ERROR) << "Unknown multicast protocol type: " << protocol;
    return std::nullopt;
  }
  key.first = StringToMulticastProtocolType(protocol).value();
  if (!StringToMulticastTechnologyType(technology).has_value()) {
    LOG(ERROR) << "Unknown multicast technology type: " << technology;
    return std::nullopt;
  }
  key.second = StringToMulticastTechnologyType(technology).value();
  return key;
}
}  // namespace

MulticastCountersService::MulticastCountersService(Datapath* datapath)
    : datapath_(datapath) {}

void MulticastCountersService::Start() {
  std::vector<std::string> protocols = {"mdns", "ssdp"};
  for (std::string const& protocol : protocols) {
    datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                              Iptables::Command::kN, {"rx_" + protocol});
    datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                              Iptables::Command::kN,
                              {"rx_ethernet_" + protocol});
    datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                              Iptables::Command::kN, {"rx_wifi_" + protocol});
  }

  std::vector<std::string> args;
  args = {"INPUT", "-d",  kMdnsMcastAddress.ToString(),
          "-p",    "udp", "--dport",
          "5353",  "-j",  "rx_mdns"};
  datapath_->ModifyIptables(IpFamily::kIPv4, Iptables::Table::kMangle,
                            Iptables::Command::kA, args);
  args = {"INPUT", "-d",  kSsdpMcastAddress.ToString(),
          "-p",    "udp", "--dport",
          "1900",  "-j",  "rx_ssdp"};
  datapath_->ModifyIptables(IpFamily::kIPv4, Iptables::Table::kMangle,
                            Iptables::Command::kA, args);
  args = {"INPUT", "-d",  kMdnsMcastAddress6.ToString(),
          "-p",    "udp", "--dport",
          "5353",  "-j",  "rx_mdns"};
  datapath_->ModifyIptables(IpFamily::kIPv6, Iptables::Table::kMangle,
                            Iptables::Command::kA, args);
  args = {"INPUT", "-d",  kSsdpMcastAddress6.ToString(),
          "-p",    "udp", "--dport",
          "1900",  "-j",  "rx_ssdp"};
  datapath_->ModifyIptables(IpFamily::kIPv6, Iptables::Table::kMangle,
                            Iptables::Command::kA, args);
}
void MulticastCountersService::Stop() {
  std::vector<std::string> args;
  args = {"INPUT", "-d",  kMdnsMcastAddress.ToString(),
          "-p",    "udp", "--dport",
          "5353",  "-j",  "rx_mdns"};
  datapath_->ModifyIptables(IpFamily::kIPv4, Iptables::Table::kMangle,
                            Iptables::Command::kD, args);
  args = {"INPUT", "-d",  kSsdpMcastAddress.ToString(),
          "-p",    "udp", "--dport",
          "1900",  "-j",  "rx_ssdp"};
  datapath_->ModifyIptables(IpFamily::kIPv4, Iptables::Table::kMangle,
                            Iptables::Command::kD, args);
  args = {"INPUT", "-d",  kMdnsMcastAddress6.ToString(),
          "-p",    "udp", "--dport",
          "5353",  "-j",  "rx_mdns"};
  datapath_->ModifyIptables(IpFamily::kIPv6, Iptables::Table::kMangle,
                            Iptables::Command::kD, args);
  args = {"INPUT", "-d",  kSsdpMcastAddress6.ToString(),
          "-p",    "udp", "--dport",
          "1900",  "-j",  "rx_ssdp"};
  datapath_->ModifyIptables(IpFamily::kIPv6, Iptables::Table::kMangle,
                            Iptables::Command::kD, args);

  std::vector<std::string> protocols = {"mdns", "ssdp"};
  for (std::string const& protocol : protocols) {
    datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                              Iptables::Command::kF, {"rx_" + protocol});
    datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                              Iptables::Command::kX,
                              {"rx_ethernet_" + protocol});
    datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                              Iptables::Command::kX, {"rx_wifi_" + protocol});
    datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                              Iptables::Command::kX, {"rx_" + protocol});
  }
}

void MulticastCountersService::OnPhysicalDeviceAdded(
    const ShillClient::Device& device) {
  std::string technology;
  if (device.type == ShillClient::Device::Type::kWifi) {
    technology = kTechnologyWifi;
  } else if (device.type == ShillClient::Device::Type::kEthernet) {
    technology = kTechnologyEthernet;
  } else {
    return;
  }

  SetupJumpRules(Iptables::Command::kA, device.ifname, technology);
  interfaces_.push_back({device.ifname, technology});
}

void MulticastCountersService::SetupJumpRules(Iptables::Command command,
                                              base::StringPiece ifname,
                                              base::StringPiece technology) {
  std::vector<std::string> args;
  for (const std::string& protocol : {"mdns", "ssdp"}) {
    args = {"rx_" + protocol, "-i", ifname.data(), "-j",
            base::JoinString({"rx_", technology, "_", protocol}, "")};
    // Skip adding jump rules when already exist.
    if (command == Iptables::Command::kA &&
        datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                                  Iptables::Command::kC, args,
                                  /*log_failures=*/false)) {
      continue;
    }

    // Skip deleting jump rules when not exist.
    if (command == Iptables::Command::kD &&
        !datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                                   Iptables::Command::kC, args,
                                   /*log_failures=*/false)) {
      LOG(WARNING) << "Jump rules does not exist, skip deleting jump rules for "
                   << ifname;
      continue;
    }

    if (!datapath_->ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                                   command, args)) {
      LOG(ERROR) << "Failed to add multicast iptables counter rules for "
                 << ifname;
    }
  }
}

std::optional<std::map<MulticastCountersService::CounterKey, uint64_t>>
MulticastCountersService::GetCounters() {
  std::map<MulticastCountersService::CounterKey, uint64_t> counters = {
      {{MulticastCountersService::MulticastProtocolType::kMdns,
        MulticastCountersService::MulticastTechnologyType::kEthernet},
       0},
      {{MulticastCountersService::MulticastProtocolType::kSsdp,
        MulticastCountersService::MulticastTechnologyType::kEthernet},
       0},
      {{MulticastCountersService::MulticastProtocolType::kMdns,
        MulticastCountersService::MulticastTechnologyType::kWifi},
       0},
      {{MulticastCountersService::MulticastProtocolType::kSsdp,
        MulticastCountersService::MulticastTechnologyType::kWifi},
       0},
  };

  // Handles counters for IPv4 and IPv6 separately and returns failure if either
  // of the procession fails, since counters for only IPv4 or IPv6 are biased.
  std::string iptables_result =
      datapath_->DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle);
  if (iptables_result.empty()) {
    LOG(ERROR) << "Failed to query IPv4 counters";
    return std::nullopt;
  }
  if (!ParseIptableOutput(iptables_result, &counters)) {
    LOG(ERROR) << "Failed to parse IPv4 counters";
    return std::nullopt;
  }

  std::string ip6tables_result =
      datapath_->DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle);
  if (ip6tables_result.empty()) {
    LOG(ERROR) << "Failed to query IPv6 counters";
    return std::nullopt;
  }
  if (!ParseIptableOutput(ip6tables_result, &counters)) {
    LOG(ERROR) << "Failed to parse IPv6 counters";
    return std::nullopt;
  }

  return counters;
}

bool MulticastCountersService::ParseIptableOutput(
    base::StringPiece output,
    std::map<MulticastCountersService::CounterKey, uint64_t>* counter) {
  const std::vector<std::string> lines = base::SplitString(
      output, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  for (auto it = lines.cbegin(); it != lines.cend(); it++) {
    // Check if a line is chain name line, if not, go to next line.
    std::string protocol;
    while (it != lines.cend() && !RE2::FullMatch(*it, *kChainLine, &protocol)) {
      it++;
    }
    if (it == lines.cend()) {
      break;
    }
    // If this is the chain we are looking for, parse counter line.
    if (protocol.empty() || (protocol == "mdns" || protocol == "ssdp")) {
      if (lines.cend() - it <= 2) {
        LOG(ERROR) << "Invalid iptables output for " << protocol;
        return false;
      }
      it += 2;

      // Checks that there are some counter rules defined.
      if (it == lines.cend() || it->empty()) {
        continue;
      }

      // The next block of lines are the counters lines for different
      // technologies.
      for (; it != lines.cend() && !it->empty(); it++) {
        const std::vector<std::string> components = base::SplitString(
            *it, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

        uint64_t packet_count;
        std::string target;
        if (!RE2::FullMatch(*it, *kCounterLine, &packet_count, &target)) {
          LOG(ERROR) << "Parse counter line failed, counter line is: " << *it;
          return false;
        }

        auto key = GetCounterKeyFromTargetName(target);
        if (key) {
          (*counter)[*key] += packet_count;
        }
      }
      if (it == lines.cend()) {
        break;
      }
    }
  }
  return true;
}
}  // namespace patchpanel
