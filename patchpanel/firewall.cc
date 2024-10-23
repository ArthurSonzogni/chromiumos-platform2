// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/firewall.h"

#include <arpa/inet.h>
#include <linux/capability.h>
#include <netinet/in.h>

#include <string>
#include <string_view>
#include <vector>

#include <base/containers/span.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "patchpanel/datapath.h"
#include "patchpanel/iptables.h"

namespace patchpanel {

const std::string ProtocolName(Protocol proto) {
  if (proto == ModifyPortRuleRequest::INVALID_PROTOCOL) {
    NOTREACHED_IN_MIGRATION() << "Unexpected L4 protocol value";
  }
  return base::ToLowerASCII(ModifyPortRuleRequest::Protocol_Name(proto));
}

Firewall::Firewall() : Firewall(MinijailedProcessRunner::GetInstance()) {}

Firewall::Firewall(MinijailedProcessRunner* process_runner)
    : process_runner_(process_runner) {}

bool Firewall::AddAcceptRules(Protocol protocol,
                              uint16_t port,
                              std::string_view interface) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  if (!AddAcceptRule(IpFamily::kIPv4, protocol, port, interface)) {
    LOG(ERROR) << "Could not add IPv4 ACCEPT rule";
    return false;
  }

  if (!AddAcceptRule(IpFamily::kIPv6, protocol, port, interface)) {
    LOG(ERROR) << "Could not add IPv6 ACCEPT rule";
    DeleteAcceptRule(IpFamily::kIPv4, protocol, port, interface);
    return false;
  }

  return true;
}

bool Firewall::DeleteAcceptRules(Protocol protocol,
                                 uint16_t port,
                                 std::string_view interface) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  bool ip4_success =
      DeleteAcceptRule(IpFamily::kIPv4, protocol, port, interface);
  bool ip6_success =
      DeleteAcceptRule(IpFamily::kIPv6, protocol, port, interface);
  return ip4_success && ip6_success;
}

bool Firewall::AddIpv4ForwardRule(
    Protocol protocol,
    const std::optional<net_base::IPv4Address>& input_ip,
    uint16_t port,
    std::string_view interface,
    const net_base::IPv4Address& dst_ip,
    uint16_t dst_port) {
  if (!ModifyIpv4DNATRule(protocol, input_ip, port, interface, dst_ip, dst_port,
                          Iptables::Command::kI)) {
    return false;
  }

  if (!ModifyIpv4ForwardChain(protocol, interface, dst_ip, dst_port,
                              Iptables::Command::kA)) {
    ModifyIpv4DNATRule(protocol, input_ip, port, interface, dst_ip, dst_port,
                       Iptables::Command::kD);
    return false;
  }

  return true;
}

bool Firewall::DeleteIpv4ForwardRule(
    Protocol protocol,
    const std::optional<net_base::IPv4Address>& input_ip,
    uint16_t port,
    std::string_view interface,
    const net_base::IPv4Address& dst_ip,
    uint16_t dst_port) {
  bool success = true;
  if (!ModifyIpv4DNATRule(protocol, input_ip, port, interface, dst_ip, dst_port,
                          Iptables::Command::kD)) {
    success = false;
  }
  if (!ModifyIpv4ForwardChain(protocol, interface, dst_ip, dst_port,
                              Iptables::Command::kD)) {
    success = false;
  }
  return success;
}

bool Firewall::ModifyIpv4DNATRule(
    Protocol protocol,
    const std::optional<net_base::IPv4Address>& input_ip,
    uint16_t port,
    std::string_view interface,
    const net_base::IPv4Address& dst_ip,
    uint16_t dst_port,
    Iptables::Command command) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  if (interface.empty()) {
    LOG(ERROR) << "Interface name should not be empty";
    return false;
  }

  if (dst_port == 0U) {
    LOG(ERROR) << "Destination port 0 is not a valid port";
    return false;
  }

  // Only support deleting existing forwarding rules or inserting rules in the
  // first position: ARC++ generic inbound DNAT rule always need to go last.
  if (command != Iptables::Command::kI && command != Iptables::Command::kD) {
    LOG(ERROR) << "Invalid iptables command '" << command << "'";
    return false;
  }

  std::vector<std::string> argv{
      "-i",
      std::string(interface),
      "-p",  // protocol
      ProtocolName(protocol),
  };
  if (input_ip) {
    argv.insert(argv.end(),
                {"-d", input_ip->ToString()});  // input destination ip
  }
  argv.insert(
      argv.end(),
      {"--dport", std::to_string(port),   // input destination port
       "-j", "DNAT", "--to-destination",  // new output destination ip:port
       base::StrCat({dst_ip.ToString(), ":", std::to_string(dst_port)}), "-w"});

  return RunIptables(IpFamily::kIPv4, Iptables::Table::kNat, command,
                     kIngressPortForwardingChain, argv);
}

bool Firewall::ModifyIpv4ForwardChain(Protocol protocol,
                                      std::string_view interface,
                                      const net_base::IPv4Address& dst_ip,
                                      uint16_t dst_port,
                                      Iptables::Command command) {
  if (interface.empty()) {
    LOG(ERROR) << "Interface name should not be empty";
    return false;
  }

  if (dst_port == 0U) {
    LOG(ERROR) << "Destination port 0 is not a valid port";
    return false;
  }

  // Order does not matter for the FORWARD chain: both -A or -I are possible.
  if (command != Iptables::Command::kA && command != Iptables::Command::kI &&
      command != Iptables::Command::kD) {
    LOG(ERROR) << "Invalid iptables command '" << command << "'";
    return false;
  }

  std::vector<std::string> argv{
      "-i",
      std::string(interface),
      "-p",  // protocol
      ProtocolName(protocol),
      "-d",  // destination ip
      dst_ip.ToString(),
      "--dport",  // destination port
      std::to_string(dst_port),
      "-j",
      "ACCEPT",
      "-w",
  };  // Wait for xtables lock.
  return RunIptables(IpFamily::kIPv4, Iptables::Table::kFilter, command,
                     "FORWARD", argv);
}

bool Firewall::AddLoopbackLockdownRules(Protocol protocol, uint16_t port) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  if (!AddLoopbackLockdownRule(IpFamily::kIPv4, protocol, port)) {
    LOG(ERROR) << "Could not add loopback IPv4 REJECT rule";
    return false;
  }

  if (!AddLoopbackLockdownRule(IpFamily::kIPv6, protocol, port)) {
    LOG(ERROR) << "Could not add loopback IPv6 REJECT rule";
    DeleteLoopbackLockdownRule(IpFamily::kIPv4, protocol, port);
    return false;
  }

  return true;
}

bool Firewall::DeleteLoopbackLockdownRules(Protocol protocol, uint16_t port) {
  if (port == 0U) {
    LOG(ERROR) << "Port 0 is not a valid port";
    return false;
  }

  bool ip4_success =
      DeleteLoopbackLockdownRule(IpFamily::kIPv4, protocol, port);
  bool ip6_success =
      DeleteLoopbackLockdownRule(IpFamily::kIPv6, protocol, port);
  return ip4_success && ip6_success;
}

bool Firewall::AddAcceptRule(IpFamily ip_family,
                             Protocol protocol,
                             uint16_t port,
                             std::string_view interface) {
  return ModifyAcceptRule(ip_family, protocol, port, interface,
                          Iptables::Command::kI);
}

bool Firewall::DeleteAcceptRule(IpFamily ip_family,
                                Protocol protocol,
                                uint16_t port,
                                std::string_view interface) {
  return ModifyAcceptRule(ip_family, protocol, port, interface,
                          Iptables::Command::kD);
}

bool Firewall::ModifyAcceptRule(IpFamily ip_family,
                                Protocol protocol,
                                uint16_t port,
                                std::string_view interface,
                                Iptables::Command command) {
  std::vector<std::string> argv{
      "-p",  // protocol
      ProtocolName(protocol),
      "--dport",  // destination port
      std::to_string(port),
  };
  if (!interface.empty()) {
    argv.insert(argv.end(), {"-i", std::string(interface)});
  }
  argv.insert(argv.end(), {"-j", "ACCEPT", "-w"});

  return RunIptables(ip_family, Iptables::Table::kFilter, command,
                     kIngressPortFirewallChain, argv);
}

bool Firewall::AddLoopbackLockdownRule(IpFamily ip_family,
                                       Protocol protocol,
                                       uint16_t port) {
  return ModifyLoopbackLockdownRule(ip_family, protocol, port,
                                    Iptables::Command::kI);
}

bool Firewall::DeleteLoopbackLockdownRule(IpFamily ip_family,
                                          Protocol protocol,
                                          uint16_t port) {
  return ModifyLoopbackLockdownRule(ip_family, protocol, port,
                                    Iptables::Command::kD);
}

bool Firewall::ModifyLoopbackLockdownRule(IpFamily ip_family,
                                          Protocol protocol,
                                          uint16_t port,
                                          Iptables::Command command) {
  std::vector<std::string> argv{
      "-p",  // protocol
      ProtocolName(protocol),
      "--dport",  // destination port
      std::to_string(port),
      "-o",  // output interface
      "lo",
      "-m",  // match extension
      "owner",
      "!",
      "--uid-owner",
      "chronos",
      "-j",
      "REJECT",
      "-w",  // Wait for xtables lock.
  };

  return RunIptables(ip_family, Iptables::Table::kFilter, command,
                     kEgressPortFirewallChain, argv);
}

bool Firewall::RunIptables(IpFamily ip_family,
                           Iptables::Table table,
                           Iptables::Command command,
                           std::string_view chain,
                           base::span<const std::string> argv) {
  if (ip_family == IpFamily::kIPv4) {
    return process_runner_->iptables(table, command, chain, argv, false) == 0;
  }

  if (ip_family == IpFamily::kIPv6) {
    return process_runner_->ip6tables(table, command, chain, argv, false) == 0;
  }

  return false;
}

}  // namespace patchpanel
