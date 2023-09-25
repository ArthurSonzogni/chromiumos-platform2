// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_FIREWALL_H_
#define PATCHPANEL_FIREWALL_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <brillo/errors/error.h>
#include <gtest/gtest_prod.h>
#include <net-base/ipv4_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/iptables.h"
#include "patchpanel/minijailed_process_runner.h"

namespace patchpanel {

using Operation = patchpanel::ModifyPortRuleRequest::Operation;
using Protocol = patchpanel::ModifyPortRuleRequest::Protocol;
using RuleType = patchpanel::ModifyPortRuleRequest::RuleType;

const std::string ProtocolName(Protocol proto);

class Firewall {
 public:
  typedef std::pair<uint16_t, std::string> Hole;

  Firewall();
  explicit Firewall(MinijailedProcessRunner* process_runner);
  Firewall(const Firewall&) = delete;
  Firewall& operator=(const Firewall&) = delete;

  virtual ~Firewall() = default;

  virtual bool AddAcceptRules(Protocol protocol,
                              uint16_t port,
                              const std::string& interface);
  virtual bool DeleteAcceptRules(Protocol protocol,
                                 uint16_t port,
                                 const std::string& interface);
  virtual bool AddLoopbackLockdownRules(Protocol protocol, uint16_t port);
  virtual bool DeleteLoopbackLockdownRules(Protocol protocol, uint16_t port);
  virtual bool AddIpv4ForwardRule(
      Protocol protocol,
      const std::optional<net_base::IPv4Address>& input_ip,
      uint16_t port,
      const std::string& interface,
      const net_base::IPv4Address& dst_ip,
      uint16_t dst_port);
  virtual bool DeleteIpv4ForwardRule(
      Protocol protocol,
      const std::optional<net_base::IPv4Address>& input_ip,
      uint16_t port,
      const std::string& interface,
      const net_base::IPv4Address& dst_ip,
      uint16_t dst_port);

 private:
  enum class IpFamily {
    kIPv4,
    kIPv6,
  };

  // Adds or removes ACCEPT chain rules to/from the filter INPUT chain.
  bool AddAcceptRule(IpFamily ip_family,
                     Protocol protocol,
                     uint16_t port,
                     const std::string& interface);
  bool DeleteAcceptRule(IpFamily ip_family,
                        Protocol protocol,
                        uint16_t port,
                        const std::string& interface);
  bool ModifyAcceptRule(IpFamily ip_family,
                        Protocol protocol,
                        uint16_t port,
                        const std::string& interface,
                        Iptables::Command command);
  // Adds or removes MASQUERADE chain rules to/from the nat PREROUTING chain.
  bool ModifyIpv4DNATRule(Protocol protocol,
                          const std::optional<net_base::IPv4Address>& input_ip,
                          uint16_t port,
                          const std::string& interface,
                          const net_base::IPv4Address& dst_ip,
                          uint16_t dst_port,
                          Iptables::Command command);
  // Adds or removes ACCEPT chain rules to/from the filter FORWARD chain.
  bool ModifyIpv4ForwardChain(Protocol protocol,
                              const std::string& interface,
                              const net_base::IPv4Address& dst_ip,
                              uint16_t dst_port,
                              Iptables::Command command);
  bool AddLoopbackLockdownRule(IpFamily ip_family,
                               Protocol protocol,
                               uint16_t port);
  bool DeleteLoopbackLockdownRule(IpFamily ip_family,
                                  Protocol protocol,
                                  uint16_t port);
  bool ModifyLoopbackLockdownRule(IpFamily ip_family,
                                  Protocol protocol,
                                  uint16_t port,
                                  Iptables::Command command);
  bool RunIptables(IpFamily ip_family,
                   Iptables::Table table,
                   Iptables::Command command,
                   std::string_view chain,
                   const std::vector<std::string>& argv);

  std::unique_ptr<MinijailedProcessRunner> process_runner_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_FIREWALL_H_
