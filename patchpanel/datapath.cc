// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/datapath.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/if_tun.h>
#include <linux/sockios.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <base/check.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/userdb_utils.h>

#include "patchpanel/adb_proxy.h"
#include "patchpanel/arc_service.h"
#include "patchpanel/dhcp_server_controller.h"
#include "patchpanel/iptables.h"
#include "patchpanel/net_util.h"

namespace patchpanel {
namespace {

using net_base::IPv4Address;
using net_base::IPv4CIDR;
using net_base::IPv6Address;
using net_base::IPv6CIDR;

// TODO(hugobenichi) Consolidate this constant definition in a single place.
constexpr pid_t kTestPID = -2;
constexpr char kDefaultIfname[] = "vmtap%d";
constexpr net_base::IPv4Address kArcAddr(100, 115, 92, 2);
constexpr net_base::IPv4Address kLocalhostAddr(127, 0, 0, 1);
constexpr char kDefaultDnsPort[] = "53";
constexpr char kChronosUid[] = "chronos";
constexpr uint16_t kAdbServerPort = 5555;

// Constants used for dropping locally originated traffic bound to an incorrect
// source IPv4 address.
constexpr char kGuestIPv4Subnet[] = "100.115.92.0/23";
// Interface name patterns matching all Cellular physical and virtual interfaces
// supported on ChromeOS.
constexpr std::array<const char*, 3> kCellularIfnamePrefixes{
    {"wwan+", "mbimmux+", "qmapmux+"}};
// Same as |kCellularIfnamePrefixes| but for other network technologies.
constexpr std::array<const char*, 4> kOtherPhysicalIfnamePrefixes{
    {"eth+", "wlan+", "mlan+", "usb+"}};

// Chains for tagging egress traffic in the OUTPUT and PREROUTING chains of the
// mangle table.
constexpr char kApplyLocalSourceMarkChain[] = "apply_local_source_mark";
constexpr char kSkipApplyVpnMarkChain[] = "skip_apply_vpn_mark";
constexpr char kApplyVpnMarkChain[] = "apply_vpn_mark";

// Chains for allowing host services to receive ingress traffic on a downstream
// interface configured with StartDownstreamNetwork.
constexpr char kAcceptDownstreamNetworkChain[] = "accept_downstream_network";

// Egress filter chain for dropping in the OUTPUT chain any local traffic
// incorrectly bound to a static IPv4 address used for ARC or Crostini.
constexpr char kDropGuestIpv4PrefixChain[] = "drop_guest_ipv4_prefix";
// Egress filter chain for preemptively dropping in the FORWARD chain any ARC or
// Crostini traffic that may not be correctly processed in SNAT.
constexpr char kDropGuestInvalidIpv4Chain[] = "drop_guest_invalid_ipv4";

// Egress nat chain for redirecting DNS queries from system services.
// TODO(b/162788331) Remove once dns-proxy has become fully operational.
constexpr char kRedirectDnsChain[] = "redirect_dns";

// OUTPUT filter chain to enforce source IP on egress IPv6 packets.
constexpr char kEnforceSourcePrefixChain[] = "enforce_ipv6_src_prefix";

// VPN egress filter chains for the filter OUTPUT and FORWARD chains.
constexpr char kVpnEgressFiltersChain[] = "vpn_egress_filters";
constexpr char kVpnAcceptChain[] = "vpn_accept";
constexpr char kVpnLockdownChain[] = "vpn_lockdown";

// IPv4 nat PREROUTING chains for forwarding ingress traffic to different types
// of hosted guests with the corresponding hierarchy.
constexpr char kApplyAutoDNATToArcChain[] = "apply_auto_dnat_to_arc";
constexpr char kApplyAutoDNATToCrostiniChain[] = "apply_auto_dnat_to_crostini";
constexpr char kApplyAutoDNATToParallelsChain[] =
    "apply_auto_dnat_to_parallels";
// nat PREROUTING chain for egress traffic from downstream guests.
constexpr char kRedirectDefaultDnsChain[] = "redirect_default_dns";
// nat OUTPUT chains for egress traffic from processes running on the host.
constexpr char kRedirectChromeDnsChain[] = "redirect_chrome_dns";
constexpr char kRedirectUserDnsChain[] = "redirect_user_dns";
// nat POSTROUTING chains for egress traffic from processes running on the host.
constexpr char kSNATChromeDnsChain[] = "snat_chrome_dns";
constexpr char kSNATUserDnsChain[] = "snat_user_dns";

// Chains for QoS.
// mangle OUTPUT and PREROUTING chains for applying fwmarks on both ingress and
// egress traffic for QoS.
// - qos_detect: Hold the rules for applying fwmarks. Jump rule to this chain is
//   in qos_detect_static and installed dynamically.
// - qos_detect_static: Hold only the jump rule to qos_detect. Jump rules to
//   this chain are in OUTPUT and PREROUTING chains and installed statically.
// The main purpose to have these two layers is for having static order of rules
// in the mangle table. Also see b/301566901.
constexpr char kQoSDetectChain[] = "qos_detect";
constexpr char kQoSDetectStaticChain[] = "qos_detect_static";
// mangle chain for holding the dynamic matching rules for DoH. Referenced in
// the qos_detect chain.
constexpr char kQoSDetectDoHChain[] = "qos_detect_doh";
// mangle POSTROUTING chain for applying DSCP fields based on fwmarks for egress
// traffic.
constexpr char kQoSApplyDSCPChain[] = "qos_apply_dscp";

// Maximum length of an iptables chain name.
constexpr int kIptablesMaxChainLength = 28;

IpFamily ConvertIpFamily(net_base::IPFamily family) {
  return (family == net_base::IPFamily::kIPv4) ? IpFamily::kIPv4
                                               : IpFamily::kIPv6;
}

TrafficSource DownstreamNetworkInfoTrafficSource(
    const DownstreamNetworkInfo& info) {
  // TODO(b/257880335): define source for LocalOnlyNetwork.
  return TrafficSource::kTetherDownstream;
}

std::string AutoDNATTargetChainName(AutoDNATTarget auto_dnat_target) {
  switch (auto_dnat_target) {
    case AutoDNATTarget::kArc:
      return kApplyAutoDNATToArcChain;
    case AutoDNATTarget::kCrostini:
      return kApplyAutoDNATToCrostiniChain;
    case AutoDNATTarget::kParallels:
      return kApplyAutoDNATToParallelsChain;
  }
}

std::ostream& operator<<(std::ostream& stream, const DeviceMode tun_tap) {
  switch (tun_tap) {
    case DeviceMode::kTun:
      return stream << "tun";
    case DeviceMode::kTap:
      return stream << "tap";
  }
}

// Returns the conventional name for the PREROUTING mangle subchain
// pertaining to the downstream interface |int_ifname|.
std::string PreroutingSubChainName(const std::string& int_ifname) {
  return "PREROUTING_" + int_ifname;
}

std::string EgressSubChainName(const std::string& ext_ifname) {
  return "egress_" + ext_ifname;
}

}  // namespace

Datapath::Datapath(System* system)
    : Datapath(new MinijailedProcessRunner(), new Firewall(), system) {}

Datapath::Datapath(MinijailedProcessRunner* process_runner,
                   Firewall* firewall,
                   System* system)
    : system_(system) {
  process_runner_.reset(process_runner);
  firewall_.reset(firewall);
}

void Datapath::Start() {
  // Restart from a clean iptables state in case of an unordered shutdown.
  ResetIptables();
  // Enable IPv4 packet forwarding
  if (!system_->SysNetSet(System::SysNet::kIPv4Forward, "1")) {
    LOG(ERROR) << "Failed to update net.ipv4.ip_forward."
               << " Guest connectivity will not work correctly.";
  }

  // Limit local port range: Android owns 47104-61000.
  // TODO(garrick): The original history behind this tweak is gone. Some
  // investigation is needed to see if it is still applicable.
  if (!system_->SysNetSet(System::SysNet::kIPLocalPortRange, "32768 47103")) {
    LOG(ERROR) << "Failed to limit local port range. Some Android features or"
               << " apps may not work correctly.";
  }

  // Enable IPv6 packet forwarding and cross-interface proxying
  if (!system_->SysNetSet(System::SysNet::kIPv6Forward, "1")) {
    LOG(ERROR) << "Failed to update net.ipv6.conf.all.forwarding."
               << " IPv6 functionality may be broken.";
  }
  if (!system_->SysNetSet(System::SysNet::kIPv6ProxyNDP, "1")) {
    LOG(ERROR) << "Failed to update net.ipv6.conf.all.proxy_ndp."
               << " IPv6 functionality may be broken.";
  }

  // Creates all "stateless" iptables chains used by patchpanel and set up
  // basic jump rules from the builtin chains. All chains that needs to carry
  // some state when patchpanel restarts (for instance: chains for
  // permission_broker rules, traffic accounting chains) are created separately.
  static struct {
    IpFamily family;
    Iptables::Table table;
    std::string chain;
  } makeCommands[] = {
      // Set up a mangle chain used in OUTPUT for applying the fwmark
      // TrafficSource tag and tagging the local traffic that should be routed
      // through a VPN.
      {IpFamily::kDual, Iptables::Table::kMangle, kApplyLocalSourceMarkChain},
      // Set up a mangle chain used in OUTPUT and PREROUTING to skip VPN fwmark
      // tagging applied through "apply_vpn_mark" chain. This is used to protect
      // DNS traffic that should go to the DNS proxy.
      {IpFamily::kDual, Iptables::Table::kMangle, kSkipApplyVpnMarkChain},
      // Sets up a mangle chain used in OUTPUT and PREROUTING for tagging "user"
      // traffic that should be routed through a VPN.
      {IpFamily::kDual, Iptables::Table::kMangle, kApplyVpnMarkChain},
      // Set up mangle chains used in OUTPUT and PREROUTING for applying fwmarks
      // for QoS.
      {IpFamily::kDual, Iptables::Table::kMangle, kQoSDetectStaticChain},
      {IpFamily::kDual, Iptables::Table::kMangle, kQoSDetectChain},
      {IpFamily::kDual, Iptables::Table::kMangle, kQoSDetectDoHChain},
      // Set up a mangle chain used in POSTROUTING for applying DSCP values for
      // QoS. QoSService controls when to add the jump rules to this chain.
      {IpFamily::kDual, Iptables::Table::kMangle, kQoSApplyDSCPChain},
      // Set up nat chains for redirecting egress DNS queries to the DNS proxy
      // instances.
      {IpFamily::kDual, Iptables::Table::kNat, kRedirectDefaultDnsChain},
      {IpFamily::kDual, Iptables::Table::kNat, kRedirectUserDnsChain},
      {IpFamily::kDual, Iptables::Table::kNat, kRedirectChromeDnsChain},
      // Set up nat chains for SNAT-ing egress DNS queries to the DNS proxy
      // instances.
      {IpFamily::kDual, Iptables::Table::kNat, kSNATChromeDnsChain},
      // For the case of non-Chrome "user" DNS queries, there is already an IPv4
      // SNAT rule with the ConnectNamespace. Only IPv6 USER SNAT is needed.
      {IpFamily::kIPv6, Iptables::Table::kNat, kSNATUserDnsChain},
      // b/178331695 Sets up a nat chain used in OUTPUT for redirecting DNS
      // queries of system services. When a VPN is connected, a query routed
      // through a physical network is redirected to the primary nameserver of
      // that network.
      {IpFamily::kIPv4, Iptables::Table::kNat, kRedirectDnsChain},
      // Set up nat chains for redirecting ingress traffic to downstream guests.
      // These chains are only created for IPv4 since downstream guests obtain
      // their own addresses for IPv6.
      {IpFamily::kIPv4, Iptables::Table::kNat, kIngressPortForwardingChain},
      {IpFamily::kIPv4, Iptables::Table::kNat, kApplyAutoDNATToArcChain},
      {IpFamily::kIPv4, Iptables::Table::kNat, kApplyAutoDNATToCrostiniChain},
      {IpFamily::kIPv4, Iptables::Table::kNat, kApplyAutoDNATToParallelsChain},
      // Create filter subchains for managing the egress firewall VPN rules.
      {IpFamily::kDual, Iptables::Table::kFilter, kVpnEgressFiltersChain},
      {IpFamily::kDual, Iptables::Table::kFilter, kVpnAcceptChain},
      {IpFamily::kDual, Iptables::Table::kFilter, kVpnLockdownChain},
      {IpFamily::kIPv4, Iptables::Table::kFilter, kDropGuestIpv4PrefixChain},
      {IpFamily::kIPv4, Iptables::Table::kFilter, kDropGuestInvalidIpv4Chain},
      // Create filter subchains for hosting permission_broker firewall rules
      {IpFamily::kDual, Iptables::Table::kFilter, kIngressPortFirewallChain},
      {IpFamily::kDual, Iptables::Table::kFilter, kEgressPortFirewallChain},
      // Create filter subchain for ingress firewall rules on downstream
      // networks (Tethering, LocalOnlyNetwork).
      {IpFamily::kDual, Iptables::Table::kFilter,
       kAcceptDownstreamNetworkChain},
      // Create OUTPUT filter chain to enforce source IP on egress IPv6 packets.
      {IpFamily::kIPv6, Iptables::Table::kFilter, kEnforceSourcePrefixChain},
  };
  for (const auto& c : makeCommands) {
    if (!AddChain(c.family, c.table, c.chain)) {
      LOG(ERROR) << "Failed to create " << c.chain << " chain in " << c.table
                 << " table";
    }
  }

  // Add all static jump commands from builtin chains to chains created by
  // patchpanel.
  static struct {
    IpFamily family;
    Iptables::Table table;
    std::string jump_from;
    std::string jump_to;
    std::optional<Iptables::Command> op;
  } jumpCommands[] = {
      {IpFamily::kDual, Iptables::Table::kMangle, "OUTPUT",
       kApplyLocalSourceMarkChain},
      {IpFamily::kDual, Iptables::Table::kMangle, "OUTPUT",
       kQoSDetectStaticChain},
      {IpFamily::kDual, Iptables::Table::kMangle, "PREROUTING",
       kQoSDetectStaticChain},
      {IpFamily::kDual, Iptables::Table::kNat, "PREROUTING",
       kRedirectDefaultDnsChain},
      // "ingress_port_forwarding" must be traversed before
      // the default "ingress_default_*" autoforwarding chains.
      {IpFamily::kIPv4, Iptables::Table::kNat, "PREROUTING",
       kIngressPortForwardingChain},
      // ARC default ingress forwarding is always first, Crostini second, and
      // Parallels VM last.
      {IpFamily::kIPv4, Iptables::Table::kNat, "PREROUTING",
       kApplyAutoDNATToArcChain},
      {IpFamily::kIPv4, Iptables::Table::kNat, "PREROUTING",
       kApplyAutoDNATToCrostiniChain},
      {IpFamily::kIPv4, Iptables::Table::kNat, "PREROUTING",
       kApplyAutoDNATToParallelsChain},
      // When VPN lockdown is enabled, a REJECT rule must stop
      // any egress traffic tagged with the |kFwmarkRouteOnVpn| intent mark.
      // This REJECT rule is added to |kVpnLockdownChain|. In addition, when VPN
      // lockdown is enabled and a VPN is connected, an ACCEPT rule protects the
      // traffic tagged with the VPN routing mark from being reject by the VPN
      // lockdown rule. This ACCEPT rule is added to |kVpnAcceptChain|.
      // Therefore, egress traffic must:
      //   - traverse kVpnAcceptChain before kVpnLockdownChain,
      //   - traverse kVpnLockdownChain before other ACCEPT rules in OUTPUT and
      //   FORWARD.
      // Finally, egress VPN filter rules must be inserted in front of the
      // OUTPUT chain to override basic rules set outside patchpanel.
      {IpFamily::kDual, Iptables::Table::kFilter, "OUTPUT",
       kVpnEgressFiltersChain, Iptables::Command::kI},
      {IpFamily::kDual, Iptables::Table::kFilter, "FORWARD",
       kVpnEgressFiltersChain},
      {IpFamily::kDual, Iptables::Table::kFilter, kVpnEgressFiltersChain,
       kVpnAcceptChain},
      {IpFamily::kDual, Iptables::Table::kFilter, kVpnEgressFiltersChain,
       kVpnLockdownChain},
      // b/196898241: To ensure that the drop chains drop_guest_ipv4_prefix and
      // drop_guest_invalid_ipv4 chain are traversed before vpn_accept and
      // vpn_lockdown, they are inserted last in front of the OUTPUT chain and
      // FORWARD chains respectively.
      {IpFamily::kIPv4, Iptables::Table::kFilter, "OUTPUT",
       kDropGuestIpv4PrefixChain, Iptables::Command::kI},
      {IpFamily::kIPv4, Iptables::Table::kFilter, "FORWARD",
       kDropGuestInvalidIpv4Chain, Iptables::Command::kI},
      // Attach ingress and egress firewall chains for permission_broker rules.
      {IpFamily::kDual, Iptables::Table::kFilter, "INPUT",
       kIngressPortFirewallChain},
      {IpFamily::kDual, Iptables::Table::kFilter, "OUTPUT",
       kEgressPortFirewallChain},
  };
  for (const auto& c : jumpCommands) {
    auto op = c.op.value_or(Iptables::Command::kA);
    if (!ModifyJumpRule(c.family, c.table, op, c.jump_from, c.jump_to,
                        /*iif=*/"", /*oif=*/"")) {
      LOG(ERROR) << "Failed to create jump rule from " << c.jump_from << " to "
                 << c.jump_to << " in " << c.table << " table";
    }
  }

  // Create a FORWARD ACCEPT rule for connections already established.
  if (process_runner_->iptables(
          Iptables::Table::kFilter, Iptables::Command::kA, "FORWARD",
          {"-m", "state", "--state", "ESTABLISHED,RELATED", "-j", "ACCEPT",
           "-w"}) != 0) {
    LOG(ERROR) << "Failed to install forwarding rule for established"
               << " connections.";
  }

  // Create a FORWARD ACCEPT rule for ICMP6.
  if (process_runner_->ip6tables(
          Iptables::Table::kFilter, Iptables::Command::kA, "FORWARD",
          {"-p", "ipv6-icmp", "-j", "ACCEPT", "-w"}) != 0)
    LOG(ERROR) << "Failed to install forwarding rule for ICMP6";

  // chromium:898210: Drop any locally originated traffic that would exit a
  // physical interface with a source IPv4 address from the subnet of IPs used
  // for VMs, containers, and connected namespaces. This is needed to prevent
  // packets leaking with an incorrect src IP when a local process binds to the
  // wrong interface.
  std::vector<std::string> prefixes;
  prefixes.insert(prefixes.end(), kCellularIfnamePrefixes.begin(),
                  kCellularIfnamePrefixes.end());
  prefixes.insert(prefixes.end(), kOtherPhysicalIfnamePrefixes.begin(),
                  kOtherPhysicalIfnamePrefixes.end());
  for (const auto& oif : prefixes) {
    if (!AddSourceIPv4DropRule(oif, kGuestIPv4Subnet)) {
      LOG(WARNING) << "Failed to set up IPv4 drop rule for src ip "
                   << kGuestIPv4Subnet << " exiting " << oif;
    }
  }

  // chromium:1050579: INVALID packets cannot be tracked by conntrack therefore
  // need to be explicitly dropped as SNAT cannot be applied to them.
  // b/196898241: To ensure that the INVALID DROP rule is traversed before
  // vpn_accept and vpn_lockdown, insert it in front of the FORWARD chain last.
  std::string snatMark =
      kFwmarkLegacySNAT.ToString() + "/" + kFwmarkLegacySNAT.ToString();
  if (process_runner_->iptables(
          Iptables::Table::kFilter, Iptables::Command::kI,
          kDropGuestInvalidIpv4Chain,
          {"-m", "mark", "--mark", snatMark, "-m", "state", "--state",
           "INVALID", "-j", "DROP", "-w"}) != 0) {
    LOG(ERROR) << "Failed to install FORWARD rule to drop INVALID packets";
  }
  // b/196899048: IPv4 TCP packets with TCP flags FIN,PSH coming from downstream
  // guests need to be dropped explicitly because SNAT will not apply to them
  // but the --state INVALID rule above will also not match for these packets.
  // crbug/1241756: Make sure that only egress FINPSH packets are dropped.
  for (const auto& oif : kCellularIfnamePrefixes) {
    if (process_runner_->iptables(
            Iptables::Table::kFilter, Iptables::Command::kI,
            kDropGuestInvalidIpv4Chain,
            {"-s", kGuestIPv4Subnet, "-p", "tcp", "--tcp-flags", "FIN,PSH",
             "FIN,PSH", "-o", oif, "-j", "DROP", "-w"}) != 0) {
      LOG(ERROR) << "Failed to install FORWARD rule to drop TCP FIN,PSH "
                    "packets egressing "
                 << oif << " interfaces";
    }
  }

  // Set static SNAT rules for any traffic originated from a guest (ARC,
  // Crostini, ...) or a connected namespace.
  // For IPv6, the SNAT rule is expected to only be triggered when static IPv6
  // is used (without SLAAC). See AddDownstreamInterfaceRules for the method
  // that sets up the SNAT mark.
  if (process_runner_->iptables(
          Iptables::Table::kNat, Iptables::Command::kA, "POSTROUTING",
          {"-m", "mark", "--mark", snatMark, "-j", "MASQUERADE", "-w"}) != 0) {
    LOG(ERROR) << "Failed to install SNAT mark rules.";
  }
  if (process_runner_->ip6tables(
          Iptables::Table::kNat, Iptables::Command::kA, "POSTROUTING",
          {"-m", "mark", "--mark", snatMark, "-j", "MASQUERADE", "-w"}) != 0) {
    LOG(ERROR) << "Failed to install SNAT mark rules.";
  }

  // Applies the routing tag saved in conntrack for any established connection
  // for sockets created in the host network namespace.
  if (!ModifyConnmarkRestore(IpFamily::kDual, "OUTPUT", Iptables::Command::kA,
                             /*iif=*/"", kFwmarkRoutingMask)) {
    LOG(ERROR) << "Failed to add OUTPUT CONNMARK restore rule";
  }

  // Add a rule for skipping apply_local_source_mark if the packet already has a
  // source mark (e.g., packets from a wireguard socket in the kernel).
  // TODO(b/190683881): This will also skip setting VPN policy bits on the
  // packet. Currently this rule will only be triggered for wireguard sockets so
  // it has no side effect now. We may need to revisit this later.
  ModifyIptables(
      IpFamily::kDual, Iptables::Table::kMangle, Iptables::Command::kA,
      kApplyLocalSourceMarkChain,
      {"-m", "mark", "!", "--mark", "0x0/" + kFwmarkAllSourcesMask.ToString(),
       "-j", "RETURN", "-w"});
  // Create rules for tagging local sources with the source tag and the vpn
  // policy tag.
  for (const auto& source : kLocalSourceTypes) {
    if (!ModifyFwmarkLocalSourceTag(Iptables::Command::kA, source)) {
      LOG(ERROR) << "Failed to create fwmark tagging rule for uid " << source
                 << " in " << kApplyLocalSourceMarkChain;
    }
  }
  // Finally add a catch-all rule for tagging any remaining local sources with
  // the SYSTEM source tag
  if (!ModifyFwmarkDefaultLocalSourceTag(Iptables::Command::kA,
                                         TrafficSource::kSystem))
    LOG(ERROR) << "Failed to set up rule tagging traffic with default source";

  // Set up jump chains to the DNS nat chains for egress traffic from local
  // processes running on the host.
  if (!ModifyRedirectDnsJumpRule(IpFamily::kDual, Iptables::Command::kA,
                                 "OUTPUT",
                                 /*ifname=*/"", kRedirectChromeDnsChain)) {
    LOG(ERROR) << "Failed to add jump rule for chrome DNS redirection";
  }
  if (!ModifyRedirectDnsJumpRule(IpFamily::kDual, Iptables::Command::kA,
                                 "OUTPUT",
                                 /*ifname=*/"", kRedirectUserDnsChain,
                                 kFwmarkRouteOnVpn, kFwmarkVpnMask,
                                 /*redirect_on_mark=*/true)) {
    LOG(ERROR) << "Failed to add jump rule for user DNS redirection";
  }
  if (!ModifyRedirectDnsJumpRule(
          IpFamily::kDual, Iptables::Command::kA, "POSTROUTING", /*ifname=*/"",
          kSNATChromeDnsChain, Fwmark::FromSource(TrafficSource::kChrome),
          kFwmarkAllSourcesMask, /*redirect_on_mark=*/true)) {
    LOG(ERROR) << "Failed to add jump rule for chrome DNS SNAT";
  }
  if (!ModifyRedirectDnsJumpRule(IpFamily::kIPv6, Iptables::Command::kA,
                                 "POSTROUTING", /*ifname=*/"",
                                 kSNATUserDnsChain, kFwmarkRouteOnVpn,
                                 kFwmarkVpnMask, /*redirect_on_mark=*/true)) {
    LOG(ERROR) << "Failed to add jump rule for user DNS SNAT";
  }

  // All local outgoing DNS traffic eligible to VPN routing should skip the VPN
  // routing chain and instead go through DNS proxy.
  if (!ModifyFwmarkSkipVpnJumpRule("OUTPUT", Iptables::Command::kA,
                                   kChronosUid)) {
    LOG(ERROR) << "Failed to add jump rule to skip VPN mark chain in mangle "
               << "OUTPUT chain";
  }

  // All local outgoing traffic eligible to VPN routing should traverse the VPN
  // marking chain.
  if (!ModifyFwmarkVpnJumpRule("OUTPUT", Iptables::Command::kA,
                               kFwmarkRouteOnVpn, kFwmarkVpnMask)) {
    LOG(ERROR) << "Failed to add jump rule to VPN chain in mangle OUTPUT chain";
  }

  // Add default IPv6 DROP rules to OUTPUT for any shill managed network
  // interface configured with StartSourceIPv6PrefixEnforcement. These rules
  // DROP any packet with global unicast or unique local source addresses that
  // did not match the specific prefix configured with
  // StartSourceIPv6PrefixEnforcement.
  if (!ModifyIptables(IpFamily::kIPv6, Iptables::Table::kFilter,
                      Iptables::Command::kA, kEnforceSourcePrefixChain,
                      {"-s", "2000::/3", "-j", "DROP", "-w"})) {
    LOG(ERROR) << "Fail to add 2000::/3 DROP rule in "
               << kEnforceSourcePrefixChain;
  }
  if (!ModifyIptables(IpFamily::kIPv6, Iptables::Table::kFilter,
                      Iptables::Command::kA, kEnforceSourcePrefixChain,
                      {"-s", "fc00::/7", "-j", "DROP", "-w"})) {
    LOG(ERROR) << "Fail to add fc00::/7 DROP rule in "
               << kEnforceSourcePrefixChain;
  }

  SetupQoSDetectChain();
  SetupQoSApplyDSCPChain();
}

void Datapath::Stop() {
  // Restore original local port range.
  // TODO(garrick): The original history behind this tweak is gone. Some
  // investigation is needed to see if it is still applicable.
  if (!system_->SysNetSet(System::SysNet::kIPLocalPortRange, "32768 61000")) {
    LOG(ERROR) << "Failed to restore local port range";
  }

  // Disable packet forwarding
  if (!system_->SysNetSet(System::SysNet::kIPv6Forward, "0"))
    LOG(ERROR) << "Failed to restore net.ipv6.conf.all.forwarding.";

  if (!system_->SysNetSet(System::SysNet::kIPv4Forward, "0"))
    LOG(ERROR) << "Failed to restore net.ipv4.ip_forward.";

  ResetIptables();
}

void Datapath::ResetIptables() {
  // If they exists, remove jump rules from built-in chains to custom chains
  // for any built-in chains that is not explicitly flushed.
  ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kFilter,
                 Iptables::Command::kD, "OUTPUT", kDropGuestIpv4PrefixChain,
                 /*iif=*/"", /*oif=*/"",
                 /*log_failures=*/false);
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                 Iptables::Command::kD, "INPUT", kIngressPortFirewallChain,
                 /*iif=*/"", /*oif=*/"",
                 /*log_failures=*/false);
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                 Iptables::Command::kD, "OUTPUT", kEgressPortFirewallChain,
                 /*iif=*/"", /*oif=*/"",
                 /*log_failures=*/false);
  ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kNat, Iptables::Command::kD,
                 "PREROUTING", kIngressPortForwardingChain, /*iif=*/"",
                 /*oif=*/"", /*log_failures=*/false);
  ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kNat, Iptables::Command::kD,
                 "PREROUTING", kApplyAutoDNATToArcChain, /*iif=*/"", /*oif=*/"",
                 /*log_failures=*/false);
  ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kNat, Iptables::Command::kD,
                 "PREROUTING", kApplyAutoDNATToCrostiniChain, /*iif=*/"",
                 /*oif=*/"", /*log_failures=*/false);
  ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kNat, Iptables::Command::kD,
                 "PREROUTING", kApplyAutoDNATToParallelsChain, /*iif=*/"",
                 /*oif=*/"", /*log_failures=*/false);
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kNat, Iptables::Command::kD,
                 "PREROUTING", kRedirectDefaultDnsChain, /*iif=*/"", /*oif=*/"",
                 /*log_failures=*/false);
  ModifyFwmarkSkipVpnJumpRule("OUTPUT", Iptables::Command::kD, kChronosUid,
                              /*log_failures=*/false);
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                 Iptables::Command::kD, "OUTPUT", kVpnEgressFiltersChain,
                 /*iif=*/"", /*oif=*/"",
                 /*log_failures=*/false);

  // Flush chains used for routing and fwmark tagging. Also delete additional
  // chains made by patchpanel. Chains used by permission broker (nat
  // PREROUTING, filter INPUT) and chains used for traffic counters (mangle
  // {rx,tx}_{<iface>, vpn}) are not flushed.
  // If there is any jump rule between from a chain to another chain that must
  // be removed, the first chain must be flushed first.
  // The "ingress_port_forwarding" chain is not flushed since it must hold port
  // forwarding rules requested by permission_broker.
  static struct {
    IpFamily family;
    Iptables::Table table;
    std::string chain;
    bool should_delete;
  } resetOps[] = {
      {IpFamily::kDual, Iptables::Table::kFilter, "FORWARD", false},
      {IpFamily::kDual, Iptables::Table::kMangle, "FORWARD", false},
      {IpFamily::kDual, Iptables::Table::kMangle, "INPUT", false},
      {IpFamily::kDual, Iptables::Table::kMangle, "OUTPUT", false},
      {IpFamily::kDual, Iptables::Table::kMangle, "POSTROUTING", false},
      {IpFamily::kDual, Iptables::Table::kMangle, "PREROUTING", false},
      {IpFamily::kDual, Iptables::Table::kMangle, kApplyLocalSourceMarkChain,
       true},
      {IpFamily::kDual, Iptables::Table::kMangle, kApplyVpnMarkChain, true},
      {IpFamily::kDual, Iptables::Table::kMangle, kSkipApplyVpnMarkChain, true},
      {IpFamily::kDual, Iptables::Table::kMangle, kQoSDetectStaticChain, true},
      {IpFamily::kDual, Iptables::Table::kMangle, kQoSDetectChain, true},
      {IpFamily::kDual, Iptables::Table::kMangle, kQoSDetectDoHChain, true},
      {IpFamily::kDual, Iptables::Table::kMangle, kQoSApplyDSCPChain, true},
      {IpFamily::kIPv4, Iptables::Table::kFilter, kDropGuestIpv4PrefixChain,
       true},
      {IpFamily::kIPv4, Iptables::Table::kFilter, kDropGuestInvalidIpv4Chain,
       true},
      {IpFamily::kDual, Iptables::Table::kFilter, kVpnEgressFiltersChain, true},
      {IpFamily::kDual, Iptables::Table::kFilter, kVpnAcceptChain, true},
      {IpFamily::kDual, Iptables::Table::kFilter, kVpnLockdownChain, true},
      {IpFamily::kDual, Iptables::Table::kFilter, kAcceptDownstreamNetworkChain,
       true},
      {IpFamily::kIPv6, Iptables::Table::kFilter, kEnforceSourcePrefixChain,
       true},
      {IpFamily::kDual, Iptables::Table::kNat, "OUTPUT", false},
      {IpFamily::kDual, Iptables::Table::kNat, "POSTROUTING", false},
      {IpFamily::kDual, Iptables::Table::kNat, kRedirectDefaultDnsChain, true},
      {IpFamily::kDual, Iptables::Table::kNat, kRedirectChromeDnsChain, true},
      {IpFamily::kDual, Iptables::Table::kNat, kRedirectUserDnsChain, true},
      {IpFamily::kDual, Iptables::Table::kNat, kSNATChromeDnsChain, true},
      {IpFamily::kIPv6, Iptables::Table::kNat, kSNATUserDnsChain, true},
      {IpFamily::kIPv4, Iptables::Table::kNat, kRedirectDnsChain, true},
      {IpFamily::kIPv4, Iptables::Table::kNat, kApplyAutoDNATToArcChain, true},
      {IpFamily::kIPv4, Iptables::Table::kNat, kApplyAutoDNATToCrostiniChain,
       true},
      {IpFamily::kIPv4, Iptables::Table::kNat, kApplyAutoDNATToParallelsChain,
       true},
  };
  for (const auto& op : resetOps) {
    // Chains to delete are custom chains and will not exist the first time
    // patchpanel starts after boot. Skip flushing and delete these chains if
    // they do not exist to avoid logging spurious error messages.
    if (op.should_delete &&
        !ModifyChain(op.family, op.table, Iptables::Command::kL, op.chain,
                     /*log_failures=*/false)) {
      continue;
    }

    if (!FlushChain(op.family, op.table, op.chain)) {
      LOG(ERROR) << "Failed to flush " << op.chain << " chain in table "
                 << op.table;
    }

    if (op.should_delete && !RemoveChain(op.family, op.table, op.chain)) {
      LOG(ERROR) << "Failed to delete " << op.chain << " chain in table "
                 << op.table;
    }
  }
}

bool Datapath::NetnsAttachName(const std::string& netns_name, pid_t netns_pid) {
  // Try first to delete any netns with name |netns_name| in case patchpanel
  // did not exit cleanly.
  if (process_runner_->ip_netns_delete(netns_name, /*log_failures=*/false) == 0)
    LOG(INFO) << "Deleted left over network namespace name " << netns_name;

  if (netns_pid == ConnectedNamespace::kNewNetnsPid)
    return process_runner_->ip_netns_add(netns_name) == 0;
  else
    return process_runner_->ip_netns_attach(netns_name, netns_pid) == 0;
}

bool Datapath::NetnsDeleteName(const std::string& netns_name) {
  return process_runner_->ip_netns_delete(netns_name) == 0;
}

bool Datapath::AddBridge(const std::string& ifname, const IPv4CIDR& cidr) {
  base::ScopedFD control_fd(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!control_fd.is_valid() ||
      system_->Ioctl(control_fd.get(), SIOCBRADDBR, ifname.c_str()) != 0) {
    LOG(ERROR) << "Failed to create bridge " << ifname;
    return false;
  }

  // Configure the persistent Chrome OS bridge interface with static IP.
  if (process_runner_->ip("addr", "add",
                          {cidr.ToString(), "brd",
                           cidr.GetBroadcast().ToString(), "dev", ifname}) !=
      0) {
    RemoveBridge(ifname);
    return false;
  }

  if (process_runner_->ip("link", "set", {ifname, "up"}) != 0) {
    RemoveBridge(ifname);
    return false;
  }

  return true;
}

void Datapath::RemoveBridge(const std::string& ifname) {
  process_runner_->ip("link", "set", {ifname, "down"});

  base::ScopedFD control_fd(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!control_fd.is_valid() ||
      system_->Ioctl(control_fd.get(), SIOCBRDELBR, ifname.c_str()) != 0) {
    LOG(ERROR) << "Failed to destroy bridge " << ifname;
  }
}

bool Datapath::AddToBridge(const std::string& br_ifname,
                           const std::string& ifname) {
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, br_ifname.c_str(), sizeof(ifr.ifr_name));
  ifr.ifr_ifindex = system_->IfNametoindex(ifname);

  base::ScopedFD control_fd(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!control_fd.is_valid() ||
      system_->Ioctl(control_fd.get(), SIOCBRADDIF, &ifr) != 0) {
    LOG(ERROR) << "Failed to add " << ifname << " to bridge " << br_ifname;
    return false;
  }

  return true;
}

std::string Datapath::AddTunTap(
    const std::string& name,
    const std::optional<MacAddress>& mac_addr,
    const std::optional<net_base::IPv4CIDR>& ipv4_cidr,
    const std::string& user,
    DeviceMode dev_mode) {
  base::ScopedFD dev = system_->OpenTunDev();
  if (!dev.is_valid()) {
    PLOG(ERROR) << "Failed to open tun device";
    return "";
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, name.empty() ? kDefaultIfname : name.c_str(),
          sizeof(ifr.ifr_name));
  switch (dev_mode) {
    case DeviceMode::kTun:
      ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
      break;
    case DeviceMode::kTap:
      ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
      break;
  }
  // If a template was given as the name, ifr_name will be updated with the
  // actual interface name.
  if (system_->Ioctl(dev.get(), TUNSETIFF, &ifr) != 0) {
    PLOG(ERROR) << "Failed to create " << dev_mode << " interface " << name;
    return "";
  }
  const char* ifname = ifr.ifr_name;

  if (system_->Ioctl(dev.get(), TUNSETPERSIST, 1) != 0) {
    PLOG(ERROR) << "Failed to persist " << dev_mode << " interface " << ifname;
    return "";
  }

  if (!user.empty()) {
    uid_t uid = 0;
    if (!brillo::userdb::GetUserInfo(user, &uid, nullptr)) {
      PLOG(ERROR) << "Unable to look up UID for " << user;
      RemoveTunTap(ifname, dev_mode);
      return "";
    }
    if (system_->Ioctl(dev.get(), TUNSETOWNER, uid) != 0) {
      PLOG(ERROR) << "Failed to set owner " << uid << " of " << dev_mode
                  << " interface " << ifname;
      RemoveTunTap(ifname, dev_mode);
      return "";
    }
  }

  // Create control socket for configuring the interface.
  base::ScopedFD sock(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!sock.is_valid()) {
    PLOG(ERROR) << "Failed to create control socket for " << dev_mode
                << " interface " << ifname;
    RemoveTunTap(ifname, dev_mode);
    return "";
  }

  if (ipv4_cidr) {
    struct sockaddr_in* addr =
        reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr);
    addr->sin_family = AF_INET;
    addr->sin_addr = ipv4_cidr->address().ToInAddr();
    if (system_->Ioctl(sock.get(), SIOCSIFADDR, &ifr) != 0) {
      PLOG(ERROR) << "Failed to set ip address for " << dev_mode
                  << " interface " << ifname << " {" << ipv4_cidr->ToString()
                  << "}";
      RemoveTunTap(ifname, dev_mode);
      return "";
    }

    struct sockaddr_in* netmask =
        reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_netmask);
    netmask->sin_family = AF_INET;
    netmask->sin_addr = ipv4_cidr->ToNetmask().ToInAddr();
    if (system_->Ioctl(sock.get(), SIOCSIFNETMASK, &ifr) != 0) {
      PLOG(ERROR) << "Failed to set netmask for " << dev_mode << " interface "
                  << ifname << " {" << ipv4_cidr->ToString() << "}";
      RemoveTunTap(ifname, dev_mode);
      return "";
    }
  }

  if (mac_addr) {
    struct sockaddr* hwaddr = &ifr.ifr_hwaddr;
    hwaddr->sa_family = ARPHRD_ETHER;
    memcpy(&hwaddr->sa_data, mac_addr.operator->(), sizeof(MacAddress));
    if (system_->Ioctl(sock.get(), SIOCSIFHWADDR, &ifr) != 0) {
      PLOG(ERROR) << "Failed to set mac address for " << dev_mode
                  << " interface " << ifname << " {"
                  << MacAddressToString(*mac_addr) << "}";
      RemoveTunTap(ifname, dev_mode);
      return "";
    }
  }

  if (system_->Ioctl(sock.get(), SIOCGIFFLAGS, &ifr) != 0) {
    PLOG(ERROR) << "Failed to get flags for " << dev_mode << " interface "
                << ifname;
    RemoveTunTap(ifname, dev_mode);
    return "";
  }

  ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
  if (system_->Ioctl(sock.get(), SIOCSIFFLAGS, &ifr) != 0) {
    PLOG(ERROR) << "Failed to enable " << dev_mode << " interface " << ifname;
    RemoveTunTap(ifname, dev_mode);
    return "";
  }

  return ifname;
}

void Datapath::RemoveTunTap(const std::string& ifname, DeviceMode dev_mode) {
  const std::string dev_mode_str =
      (dev_mode == DeviceMode::kTun) ? "tun" : "tap";
  process_runner_->ip("tuntap", "del", {ifname, "mode", dev_mode_str},
                      /*as_patchpanel_user=*/true);
}

bool Datapath::ConnectVethPair(pid_t netns_pid,
                               const std::string& netns_name,
                               const std::string& veth_ifname,
                               const std::string& peer_ifname,
                               const MacAddress& remote_mac_addr,
                               const IPv4CIDR& remote_ipv4_cidr,
                               const std::optional<IPv6CIDR>& remote_ipv6_cidr,
                               bool remote_multicast_flag) {
  // Set up the virtual pair across the current namespace and |netns_name|.
  if (!AddVirtualInterfacePair(netns_name, veth_ifname, peer_ifname)) {
    LOG(ERROR) << "Failed to create veth pair " << veth_ifname << ","
               << peer_ifname;
    return false;
  }

  // Configure the remote veth in namespace |netns_name|.
  {
    auto ns = ScopedNS::EnterNetworkNS(netns_name);
    if (!ns && netns_pid != kTestPID) {
      LOG(ERROR)
          << "Cannot create virtual link -- invalid container namespace?";
      return false;
    }

    if (!ConfigureInterface(peer_ifname, remote_mac_addr, remote_ipv4_cidr,
                            remote_ipv6_cidr, /*up=*/true,
                            remote_multicast_flag)) {
      LOG(ERROR) << "Failed to configure interface " << peer_ifname;
      RemoveInterface(peer_ifname);
      return false;
    }
  }

  if (!ToggleInterface(veth_ifname, /*up=*/true)) {
    LOG(ERROR) << "Failed to bring up interface " << veth_ifname;
    RemoveInterface(veth_ifname);
    return false;
  }

  return true;
}

void Datapath::RestartIPv6() {
  if (!system_->SysNetSet(System::SysNet::kIPv6Disable, "1")) {
    LOG(ERROR) << "Failed to disable IPv6";
  }
  if (!system_->SysNetSet(System::SysNet::kIPv6Disable, "0")) {
    LOG(ERROR) << "Failed to re-enable IPv6";
  }
}

bool Datapath::AddVirtualInterfacePair(const std::string& netns_name,
                                       const std::string& veth_ifname,
                                       const std::string& peer_ifname) {
  return process_runner_->ip("link", "add",
                             {veth_ifname, "type", "veth", "peer", "name",
                              peer_ifname, "netns", netns_name}) == 0;
}

bool Datapath::ToggleInterface(const std::string& ifname, bool up) {
  const std::string link = up ? "up" : "down";
  return process_runner_->ip("link", "set", {ifname, link}) == 0;
}

bool Datapath::ConfigureInterface(const std::string& ifname,
                                  std::optional<MacAddress> mac_addr,
                                  const IPv4CIDR& ipv4_cidr,
                                  const std::optional<IPv6CIDR>& ipv6_cidr,
                                  bool up,
                                  bool enable_multicast) {
  if (process_runner_->ip(
          "addr", "add",
          {ipv4_cidr.ToString(), "brd", ipv4_cidr.GetBroadcast().ToString(),
           "dev", ifname}) != 0) {
    return false;
  }
  if (ipv6_cidr &&
      process_runner_->ip("addr", "add",
                          {ipv6_cidr->ToString(), "dev", ifname}) != 0) {
    return false;
  }

  std::vector<std::string> iplink_args{
      "dev",
      ifname,
      up ? "up" : "down",
  };
  if (mac_addr) {
    iplink_args.insert(iplink_args.end(),
                       {"addr", MacAddressToString(*mac_addr)});
  }
  iplink_args.insert(iplink_args.end(),
                     {"multicast", enable_multicast ? "on" : "off"});
  return process_runner_->ip("link", "set", iplink_args) == 0;
}

void Datapath::RemoveInterface(const std::string& ifname) {
  process_runner_->ip("link", "delete", {ifname}, /*as_patchpanel_user=*/false,
                      /*log_failures=*/false);
}

bool Datapath::AddSourceIPv4DropRule(const std::string& oif,
                                     const std::string& src_ip) {
  return process_runner_->iptables(
             Iptables::Table::kFilter, Iptables::Command::kI,
             kDropGuestIpv4PrefixChain,
             {"-o", oif, "-s", src_ip, "-j", "DROP", "-w"}) == 0;
}

bool Datapath::StartRoutingNamespace(const ConnectedNamespace& nsinfo) {
  // Veth interface configuration and client routing configuration:
  //  - attach a name to the client namespace (or create a new named namespace
  //    if no client is specified).
  //  - create veth pair across the current namespace and the client namespace.
  //  - configure IPv4 address on remote veth inside client namespace.
  //  - configure IPv4 address on local veth inside host namespace.
  //  - add a default IPv4 /0 route sending traffic to that remote veth.
  if (!NetnsAttachName(nsinfo.netns_name, nsinfo.pid)) {
    LOG(ERROR) << "Failed to attach name " << nsinfo.netns_name
               << " to namespace pid " << nsinfo.pid;
    return false;
  }

  if (!ConnectVethPair(
          nsinfo.pid, nsinfo.netns_name, nsinfo.host_ifname, nsinfo.peer_ifname,
          nsinfo.peer_mac_addr, nsinfo.peer_ipv4_cidr,
          nsinfo.static_ipv6_config
              ? std::make_optional(nsinfo.static_ipv6_config->peer_cidr)
              : std::nullopt,
          /*enable_multicast=*/false)) {
    LOG(ERROR) << "Failed to create veth pair for"
                  " namespace pid "
               << nsinfo.pid;
    NetnsDeleteName(nsinfo.netns_name);
    return false;
  }

  if (!ConfigureInterface(
          nsinfo.host_ifname, nsinfo.host_mac_addr, nsinfo.host_ipv4_cidr,
          nsinfo.static_ipv6_config
              ? std::make_optional(nsinfo.static_ipv6_config->host_cidr)
              : std::nullopt,
          /*up=*/true,
          /*enable_multicast=*/false)) {
    LOG(ERROR) << "Cannot configure host interface " << nsinfo.host_ifname;
    RemoveInterface(nsinfo.host_ifname);
    NetnsDeleteName(nsinfo.netns_name);
    return false;
  }

  {
    auto ns = ScopedNS::EnterNetworkNS(nsinfo.netns_name);
    if (!ns && nsinfo.pid != kTestPID) {
      LOG(ERROR) << "Invalid namespace pid " << nsinfo.pid;
      RemoveInterface(nsinfo.host_ifname);
      NetnsDeleteName(nsinfo.netns_name);
      return false;
    }

    if (!AddIPv4Route(nsinfo.host_ipv4_cidr.address(), /*subnet_cidr=*/{})) {
      LOG(ERROR) << "Failed to add default /0 route to " << nsinfo.host_ifname
                 << " inside namespace pid " << nsinfo.pid;
      RemoveInterface(nsinfo.host_ifname);
      NetnsDeleteName(nsinfo.netns_name);
      return false;
    }

    if (nsinfo.static_ipv6_config &&
        !AddIPv6Route(nsinfo.static_ipv6_config->host_cidr.address(),
                      /*subnet_cidr=*/{})) {
      LOG(ERROR) << "Failed to add IPv6 default /0 route to "
                 << nsinfo.host_ifname << " inside namespace pid "
                 << nsinfo.pid;
      RemoveInterface(nsinfo.host_ifname);
      NetnsDeleteName(nsinfo.netns_name);
      return false;
    }
  }

  // Host namespace routing configuration
  //  - ingress: route added automatically by kernel when adding the device's
  //             address with prefix.
  //  - egress: - allow forwarding for traffic outgoing |host_ifname|.
  //            - add SNAT mark 0x1/0x1 for traffic outgoing |host_ifname|.
  //  Note that by default unsolicited ingress traffic is not forwarded to the
  //  client namespace unless the client specifically set port forwarding
  //  through permission_broker DBus APIs.

  if (!nsinfo.outbound_ifname.empty()) {
    if (!nsinfo.current_outbound_device) {
      LOG(ERROR) << __func__ << ": No shill Device known for ConnectNamespace "
                 << nsinfo;
      return false;
    }
    StartRoutingDevice(*nsinfo.current_outbound_device, nsinfo.host_ifname,
                       nsinfo.source, nsinfo.static_ipv6_config.has_value());
  } else if (!nsinfo.route_on_vpn) {
    StartRoutingDeviceAsSystem(nsinfo.host_ifname, nsinfo.source,
                               nsinfo.static_ipv6_config.has_value());
  } else {
    StartRoutingDeviceAsUser(
        nsinfo.host_ifname, nsinfo.source, nsinfo.host_ipv4_cidr.address(),
        nsinfo.peer_ipv4_cidr.address(),
        nsinfo.static_ipv6_config
            ? std::make_optional(nsinfo.static_ipv6_config->host_cidr.address())
            : std::nullopt,
        nsinfo.static_ipv6_config
            ? std::make_optional(nsinfo.static_ipv6_config->peer_cidr.address())
            : std::nullopt);
  }
  return true;
}

void Datapath::StopRoutingNamespace(const ConnectedNamespace& nsinfo) {
  StopRoutingDevice(nsinfo.host_ifname);
  RemoveInterface(nsinfo.host_ifname);
  NetnsDeleteName(nsinfo.netns_name);
}

bool Datapath::ModifyChromeDnsRedirect(IpFamily family,
                                       const DnsRedirectionRule& rule,
                                       Iptables::Command op) {
  // Validate nameservers.
  for (const auto& nameserver : rule.nameservers) {
    if (ConvertIpFamily(nameserver.GetFamily()) != family) {
      LOG(ERROR) << "Invalid nameserver address: " << nameserver
                 << ", the expected family is "
                 << (family == IpFamily::kIPv4 ? "IPv4" : "IPv6");
      return false;
    }
  }

  bool success = true;
  for (const auto& protocol : {"udp", "tcp"}) {
    for (size_t i = 0; i < rule.nameservers.size(); i++) {
      std::vector<std::string> args{
          "-p",
          protocol,
          "--dport",  // input destination port
          kDefaultDnsPort,
          "-m",
          "owner",
          "--uid-owner",
          kChronosUid,
      };

      // If there are multiple destination IPs, forward to them in a round robin
      // fashion with statistics module.
      if (rule.nameservers.size() > 1) {
        std::initializer_list<std::string> statistic_args = {
            "-m",       "statistic",
            "--mode",   "nth",
            "--every",  std::to_string(rule.nameservers.size() - i),
            "--packet", "0",
        };
        args.insert(args.end(), statistic_args);
      }
      args.insert(args.end(), {
                                  "-j",
                                  "DNAT",
                                  "--to-destination",
                                  rule.nameservers[i].ToString(),
                                  "-w",
                              });
      if (!ModifyIptables(family, Iptables::Table::kNat, op,
                          kRedirectChromeDnsChain, args)) {
        success = false;
      }
    }
  }
  if (!ModifyDnsProxyMasquerade(family, op, kSNATChromeDnsChain)) {
    success = false;
  }
  return success;
}

bool Datapath::ModifyDnsProxyDNAT(IpFamily family,
                                  const DnsRedirectionRule& rule,
                                  Iptables::Command op,
                                  const std::string& ifname,
                                  const std::string& chain) {
  bool success = true;
  for (const auto& protocol : {"udp", "tcp"}) {
    std::vector<std::string> args;
    if (!ifname.empty()) {
      args.insert(args.end(), {"-i", ifname});
    }
    args.insert(args.end(),
                {"-p", protocol, "--dport", kDefaultDnsPort, "-j", "DNAT",
                 "--to-destination", rule.proxy_address.ToString(), "-w"});
    if (!ModifyIptables(family, Iptables::Table::kNat, op, chain, args)) {
      success = false;
    }
  }
  return success;
}

bool Datapath::ModifyDnsProxyMasquerade(IpFamily family,
                                        Iptables::Command op,
                                        const std::string& chain) {
  bool success = true;
  for (const auto& protocol : {"udp", "tcp"}) {
    std::vector<std::string> args = {
        "-p", protocol, "--dport", kDefaultDnsPort, "-j", "MASQUERADE", "-w"};
    if (!ModifyIptables(family, Iptables::Table::kNat, op, chain, args)) {
      success = false;
    }
  }
  return success;
}

bool Datapath::StartDnsRedirection(const DnsRedirectionRule& rule) {
  const IpFamily family = ConvertIpFamily(rule.proxy_address.GetFamily());
  switch (rule.type) {
    case patchpanel::SetDnsRedirectionRuleRequest::DEFAULT: {
      if (!ModifyDnsProxyDNAT(family, rule, Iptables::Command::kA,
                              rule.input_ifname, kRedirectDefaultDnsChain)) {
        LOG(ERROR) << "Failed to add DNS DNAT rule for " << rule.input_ifname;
        return false;
      }
      return true;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::ARC: {
      return true;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::USER: {
      // Start protecting DNS traffic from VPN fwmark tagging.
      if (!ModifyDnsRedirectionSkipVpnRule(family, Iptables::Command::kA)) {
        LOG(ERROR) << "Failed to add VPN skip rule for DNS proxy";
        return false;
      }

      // Add DNS redirect rules for chrome traffic.
      if (!ModifyChromeDnsRedirect(family, rule, Iptables::Command::kA)) {
        LOG(ERROR) << "Failed to add chrome DNS DNAT rule";
        return false;
      }

      // Add DNS redirect rule for user traffic.
      if (!ModifyDnsProxyDNAT(family, rule, Iptables::Command::kA,
                              /*ifname=*/"", kRedirectUserDnsChain)) {
        LOG(ERROR) << "Failed to add user DNS DNAT rule";
        return false;
      }

      // Add MASQUERADE rule for user traffic.
      if (family == IpFamily::kIPv6 &&
          !ModifyDnsProxyMasquerade(family, Iptables::Command::kA,
                                    kSNATUserDnsChain)) {
        LOG(ERROR) << "Failed to add user DNS MASQUERADE rule";
        return false;
      }

      return true;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION: {
      if (!ModifyDnsExcludeDestinationRule(family, rule, Iptables::Command::kI,
                                           kRedirectChromeDnsChain)) {
        LOG(ERROR) << "Failed to add Chrome DNS exclude rule";
        return false;
      }
      if (!ModifyDnsExcludeDestinationRule(family, rule, Iptables::Command::kI,
                                           kRedirectUserDnsChain)) {
        LOG(ERROR) << "Failed to add user DNS exclude rule";
        return false;
      }
      return true;
    }
    default:
      LOG(ERROR) << "Invalid DNS proxy type " << rule;
      return false;
  }
}

void Datapath::StopDnsRedirection(const DnsRedirectionRule& rule) {
  const IpFamily family = ConvertIpFamily(rule.proxy_address.GetFamily());

  // Whenever the client that requested the rule closes the fd, the requested
  // rule will be deleted. There is a delay between fd closing time and rule
  // removal time. This prevents deletion of the rules by flushing the chains.
  switch (rule.type) {
    case patchpanel::SetDnsRedirectionRuleRequest::DEFAULT: {
      ModifyDnsProxyDNAT(family, rule, Iptables::Command::kD, rule.input_ifname,
                         kRedirectDefaultDnsChain);
      break;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::ARC: {
      break;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::USER: {
      ModifyChromeDnsRedirect(family, rule, Iptables::Command::kD);
      ModifyDnsProxyDNAT(family, rule, Iptables::Command::kD, /*ifname=*/"",
                         kRedirectUserDnsChain);
      ModifyDnsRedirectionSkipVpnRule(family, Iptables::Command::kD);
      if (family == IpFamily::kIPv6) {
        ModifyDnsProxyMasquerade(family, Iptables::Command::kD,
                                 kSNATUserDnsChain);
      }
      break;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION: {
      ModifyDnsExcludeDestinationRule(family, rule, Iptables::Command::kD,
                                      kRedirectChromeDnsChain);
      ModifyDnsExcludeDestinationRule(family, rule, Iptables::Command::kD,
                                      kRedirectUserDnsChain);
      break;
    }
    default:
      LOG(ERROR) << "Invalid DNS proxy type " << rule;
  }
}

void Datapath::AddDownstreamInterfaceRules(const std::string& int_ifname,
                                           TrafficSource source,
                                           bool static_ipv6) {
  if (!ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                      Iptables::Command::kA, "FORWARD", "ACCEPT", /*iif=*/"",
                      int_ifname)) {
    LOG(ERROR) << "Failed to enable IP forwarding from " << int_ifname;
  }

  if (!ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                      Iptables::Command::kA, "FORWARD", "ACCEPT", int_ifname,
                      /*oif=*/"")) {
    LOG(ERROR) << "Failed to enable IP forwarding to " << int_ifname;
  }

  std::string subchain = PreroutingSubChainName(int_ifname);
  // This can fail if patchpanel did not stopped correctly or failed to cleanup
  // the chain when |int_ifname| was previously deleted.
  if (!AddChain(IpFamily::kDual, Iptables::Table::kMangle, subchain))
    LOG(ERROR) << "Failed to create mangle chain " << subchain;
  // Make sure the chain is empty if patchpanel did not cleaned correctly that
  // chain before.
  if (!FlushChain(IpFamily::kDual, Iptables::Table::kMangle, subchain)) {
    LOG(ERROR) << "Could not flush " << subchain;
  }
  if (!ModifyJumpRule(IpFamily::kDual, Iptables::Table::kMangle,
                      Iptables::Command::kA, "PREROUTING", subchain, int_ifname,
                      /*oif=*/"")) {
    LOG(ERROR) << "Could not add jump rule from mangle PREROUTING to "
               << subchain;
  }
  // IPv4 traffic from all downstream interfaces should be tagged to go through
  // SNAT.
  if (!ModifyFwmark(IpFamily::kIPv4, subchain, Iptables::Command::kA, "", "", 0,
                    kFwmarkLegacySNAT, kFwmarkLegacySNAT)) {
    LOG(ERROR) << "Failed to add fwmark SNAT tagging rule for " << int_ifname;
  }
  // IPv6 traffic from all downstream interfaces should be tagged to go through
  // SNAT if NAT66 is used (see ConnectNamespace |static_ipv6|).
  if (static_ipv6 &&
      !ModifyFwmark(IpFamily::kIPv6, subchain, Iptables::Command::kA, "", "", 0,
                    kFwmarkLegacySNAT, kFwmarkLegacySNAT)) {
    LOG(ERROR) << "Failed to add fwmark SNAT tagging rule for " << int_ifname;
  }

  if (!ModifyFwmarkSourceTag(subchain, Iptables::Command::kA, source)) {
    LOG(ERROR) << "Failed to add fwmark tagging rule for source " << source
               << " in " << subchain;
  }
}

void Datapath::StartRoutingDevice(const ShillClient::Device& shill_device,
                                  const std::string& int_ifname,
                                  TrafficSource source,
                                  bool static_ipv6) {
  const std::string& ext_ifname = shill_device.ifname;
  AddDownstreamInterfaceRules(int_ifname, source, static_ipv6);
  // If |ext_ifname| is not null, mark egress traffic with the
  // fwmark routing tag corresponding to |ext_ifname|.
  int ifindex = system_->IfNametoindex(ext_ifname);
  if (ifindex == 0) {
    LOG(ERROR) << "Failed to retrieve interface index of " << ext_ifname;
    return;
  }
  auto routing_mark = Fwmark::FromIfIndex(ifindex);
  if (!routing_mark.has_value()) {
    LOG(ERROR) << "Failed to compute fwmark value of interface " << ext_ifname
               << " with index " << ifindex;
    return;
  }
  std::string subchain = PreroutingSubChainName(int_ifname);
  if (!ModifyFwmarkRoutingTag(subchain, Iptables::Command::kA,
                              routing_mark.value())) {
    LOG(ERROR) << "Failed to add fwmark routing tag for " << ext_ifname << "<-"
               << int_ifname << " in " << subchain;
  }
}

void Datapath::StartRoutingDeviceAsSystem(const std::string& int_ifname,
                                          TrafficSource source,
                                          bool static_ipv6) {
  AddDownstreamInterfaceRules(int_ifname, source, static_ipv6);

  // Set up a CONNMARK restore rule in PREROUTING to apply any fwmark routing
  // tag saved for the current connection, and rely on implicit routing to the
  // default physical network otherwise.
  std::string subchain = PreroutingSubChainName(int_ifname);
  if (!ModifyConnmarkRestore(IpFamily::kDual, subchain, Iptables::Command::kA,
                             /*iif=*/"", kFwmarkRoutingMask)) {
    LOG(ERROR) << "Failed to add CONNMARK restore rule in " << subchain;
  }
}

void Datapath::StartRoutingDeviceAsUser(
    const std::string& int_ifname,
    TrafficSource source,
    const IPv4Address& int_ipv4_addr,
    std::optional<net_base::IPv4Address> peer_ipv4_addr,
    std::optional<IPv6Address> int_ipv6_addr,
    std::optional<net_base::IPv6Address> peer_ipv6_addr) {
  AddDownstreamInterfaceRules(int_ifname, source, peer_ipv6_addr.has_value());

  // Set up a CONNMARK restore rule in PREROUTING to apply any fwmark routing
  // tag saved for the current connection, and rely on implicit routing to the
  // default logical network otherwise.
  std::string subchain = PreroutingSubChainName(int_ifname);
  if (!ModifyConnmarkRestore(IpFamily::kDual, subchain, Iptables::Command::kA,
                             /*iif=*/"", kFwmarkRoutingMask)) {
    LOG(ERROR) << "Failed to add CONNMARK restore rule in " << subchain;
  }

  // Explicitly bypass VPN fwmark tagging rules on returning traffic of a
  // connected namespace. This allows the return traffic to reach the local
  // source. Connected namespace interface can be identified by checking if
  // the value of |peer_ipv4_addr| or |peer_ipv6_addr| is not empty.
  if (peer_ipv4_addr &&
      process_runner_->iptables(
          Iptables::Table::kMangle, Iptables::Command::kA, subchain,
          {"-s", peer_ipv4_addr->ToString(), "-d", int_ipv4_addr.ToString(),
           "-j", "ACCEPT", "-w"}) != 0) {
    LOG(ERROR) << "Failed to add connected namespace IPv4 VPN bypass rule";
  }
  if (peer_ipv6_addr && int_ipv6_addr &&
      process_runner_->ip6tables(
          Iptables::Table::kMangle, Iptables::Command::kA, subchain,
          {"-s", peer_ipv6_addr->ToString(), "-d", int_ipv6_addr->ToString(),
           "-j", "ACCEPT", "-w"}) != 0) {
    LOG(ERROR) << "Failed to add connected namespace IPv6 VPN bypass rule";
  }

  // The jump rule below should not be applied for traffic from a
  // ConnectNamespace traffic that needs DNS to go to the VPN
  // (ConnectNamespace of the DNS default instance).
  if (!peer_ipv4_addr &&
      !ModifyJumpRule(IpFamily::kDual, Iptables::Table::kMangle,
                      Iptables::Command::kA, subchain, kSkipApplyVpnMarkChain,
                      /*iif=*/"", /*oif=*/"")) {
    LOG(ERROR) << "Failed to add jump rule to DNS proxy VPN chain for "
               << int_ifname;
  }

  // Forwarded traffic from downstream interfaces routed to the logical
  // default network is eligible to be routed through a VPN.
  if (!ModifyFwmarkVpnJumpRule(subchain, Iptables::Command::kA, {}, {}))
    LOG(ERROR) << "Failed to add jump rule to VPN chain for " << int_ifname;
}

void Datapath::StopRoutingDevice(const std::string& int_ifname) {
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                 Iptables::Command::kD, "FORWARD", "ACCEPT", /*iif=*/"",
                 int_ifname);
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                 Iptables::Command::kD, "FORWARD", "ACCEPT", int_ifname,
                 /*oif=*/"");

  std::string subchain = PreroutingSubChainName(int_ifname);
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kMangle,
                 Iptables::Command::kD, "PREROUTING", subchain, int_ifname,
                 /*oif=*/"");
  FlushChain(IpFamily::kDual, Iptables::Table::kMangle, subchain);
  RemoveChain(IpFamily::kDual, Iptables::Table::kMangle, subchain);
}

void Datapath::AddInboundIPv4DNAT(AutoDNATTarget auto_dnat_target,
                                  const ShillClient::Device& shill_device,
                                  const IPv4Address& ipv4_addr) {
  const std::string ipv4_addr_str = ipv4_addr.ToString();
  const std::string chain = AutoDNATTargetChainName(auto_dnat_target);
  // Direct ingress IP traffic to existing sockets.
  bool success = true;
  if (process_runner_->iptables(Iptables::Table::kNat, Iptables::Command::kA,
                                chain,
                                {"-i", shill_device.ifname, "-m", "socket",
                                 "--nowildcard", "-j", "ACCEPT", "-w"}) != 0) {
    success = false;
  }

  // Direct ingress TCP & UDP traffic to ARC interface for new connections.
  if (process_runner_->iptables(
          Iptables::Table::kNat, Iptables::Command::kA, chain,
          {"-i", shill_device.ifname, "-p", "tcp", "-j", "DNAT",
           "--to-destination", ipv4_addr_str, "-w"}) != 0) {
    success = false;
  }
  if (process_runner_->iptables(
          Iptables::Table::kNat, Iptables::Command::kA, chain,
          {"-i", shill_device.ifname, "-p", "udp", "-j", "DNAT",
           "--to-destination", ipv4_addr_str, "-w"}) != 0) {
    success = false;
  }

  if (!success) {
    LOG(ERROR) << "Failed to configure ingress DNAT rules on "
               << shill_device.ifname << " to " << ipv4_addr_str;
    RemoveInboundIPv4DNAT(auto_dnat_target, shill_device, ipv4_addr);
  }
}

void Datapath::RemoveInboundIPv4DNAT(AutoDNATTarget auto_dnat_target,
                                     const ShillClient::Device& shill_device,
                                     const IPv4Address& ipv4_addr) {
  const std::string ipv4_addr_str = ipv4_addr.ToString();
  const std::string chain = AutoDNATTargetChainName(auto_dnat_target);
  process_runner_->iptables(Iptables::Table::kNat, Iptables::Command::kD, chain,
                            {"-i", shill_device.ifname, "-p", "udp", "-j",
                             "DNAT", "--to-destination", ipv4_addr_str, "-w"});
  process_runner_->iptables(Iptables::Table::kNat, Iptables::Command::kD, chain,
                            {"-i", shill_device.ifname, "-p", "tcp", "-j",
                             "DNAT", "--to-destination", ipv4_addr_str, "-w"});
  process_runner_->iptables(Iptables::Table::kNat, Iptables::Command::kD, chain,
                            {"-i", shill_device.ifname, "-m", "socket",
                             "--nowildcard", "-j", "ACCEPT", "-w"});
}

bool Datapath::AddRedirectDnsRule(const ShillClient::Device& shill_device,
                                  const std::string dns_ipv4_addr) {
  const std::string& ifname = shill_device.ifname;
  bool success = true;
  success &= RemoveRedirectDnsRule(shill_device);
  // Use Insert operation to ensure that the new DNS address is used first.
  success &= ModifyRedirectDnsDNATRule(Iptables::Command::kI, "tcp", ifname,
                                       dns_ipv4_addr);
  success &= ModifyRedirectDnsDNATRule(Iptables::Command::kI, "udp", ifname,
                                       dns_ipv4_addr);
  physical_dns_addresses_[ifname] = dns_ipv4_addr;
  return success;
}

bool Datapath::RemoveRedirectDnsRule(const ShillClient::Device& shill_device) {
  const std::string& ifname = shill_device.ifname;
  const auto it = physical_dns_addresses_.find(ifname);
  if (it == physical_dns_addresses_.end())
    return true;

  bool success = true;
  success &= ModifyRedirectDnsDNATRule(Iptables::Command::kD, "tcp", ifname,
                                       it->second);
  success &= ModifyRedirectDnsDNATRule(Iptables::Command::kD, "udp", ifname,
                                       it->second);
  physical_dns_addresses_.erase(it);
  return success;
}

bool Datapath::ModifyRedirectDnsDNATRule(Iptables::Command op,
                                         const std::string& protocol,
                                         const std::string& ifname,
                                         const std::string& dns_ipv4_addr) {
  std::vector<std::string> args = {
      "-p", protocol, "--dport",          "53",          "-o", ifname,
      "-j", "DNAT",   "--to-destination", dns_ipv4_addr, "-w"};
  return ModifyIptables(IpFamily::kIPv4, Iptables::Table::kNat, op,
                        kRedirectDnsChain, args);
}

bool Datapath::ModifyRedirectDnsJumpRule(IpFamily family,
                                         Iptables::Command op,
                                         const std::string& chain,
                                         const std::string& ifname,
                                         const std::string& target_chain,
                                         Fwmark mark,
                                         Fwmark mask,
                                         bool redirect_on_mark) {
  std::vector<std::string> args;
  if (!ifname.empty()) {
    args.insert(args.end(), {"-i", ifname});
  }
  if (mark.Value() != 0 && mask.Value() != 0) {
    args.insert(args.end(), {"-m", "mark"});
    if (!redirect_on_mark) {
      args.push_back("!");
    }
    args.insert(args.end(),
                {"--mark", mark.ToString() + "/" + mask.ToString()});
  }
  args.insert(args.end(), {"-j", target_chain, "-w"});
  return ModifyIptables(family, Iptables::Table::kNat, op, chain, args);
}

bool Datapath::ModifyDnsRedirectionSkipVpnRule(IpFamily family,
                                               Iptables::Command op) {
  bool success = true;
  for (const auto& protocol : {"udp", "tcp"}) {
    std::vector<std::string> args = {
        "-p", protocol, "--dport", kDefaultDnsPort, "-j", "ACCEPT", "-w",
    };
    if (!ModifyIptables(family, Iptables::Table::kMangle, op,
                        kSkipApplyVpnMarkChain, args)) {
      success = false;
    }
  }
  return success;
}

bool Datapath::ModifyDnsExcludeDestinationRule(IpFamily family,
                                               const DnsRedirectionRule& rule,
                                               Iptables::Command op,
                                               const std::string& chain) {
  bool success = true;
  for (const auto& protocol : {"udp", "tcp"}) {
    std::vector<std::string> args = {
        "-p",
        protocol,
        "!",
        "-d",
        rule.proxy_address.ToString(),
        "--dport",
        kDefaultDnsPort,
        "-j",
        "RETURN",
        "-w",
    };
    if (!ModifyIptables(family, Iptables::Table::kNat, op, chain, args)) {
      success = false;
    }
  }
  return success;
}

bool Datapath::MaskInterfaceFlags(const std::string& ifname,
                                  uint16_t on,
                                  uint16_t off) {
  base::ScopedFD sock(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!sock.is_valid()) {
    PLOG(ERROR) << "Failed to create control socket";
    return false;
  }
  ifreq ifr;
  snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname.c_str());
  if (system_->Ioctl(sock.get(), SIOCGIFFLAGS, &ifr) < 0) {
    PLOG(WARNING) << "ioctl() failed to get interface flag on " << ifname;
    return false;
  }
  ifr.ifr_flags |= on;
  ifr.ifr_flags &= ~off;
  if (system_->Ioctl(sock.get(), SIOCSIFFLAGS, &ifr) < 0) {
    PLOG(WARNING) << "ioctl() failed to set flag 0x" << std::hex << on
                  << " unset flag 0x" << std::hex << off << " on " << ifname;
    return false;
  }
  return true;
}

bool Datapath::AddIPv6HostRoute(
    const std::string& ifname,
    const net_base::IPv6CIDR& ipv6_cidr,
    const std::optional<net_base::IPv6Address>& src_addr) {
  if (src_addr) {
    return process_runner_->ip6("route", "replace",
                                {ipv6_cidr.ToString(), "dev", ifname, "src",
                                 src_addr->ToString()}) == 0;
  } else {
    return process_runner_->ip6("route", "replace",
                                {ipv6_cidr.ToString(), "dev", ifname}) == 0;
  }
}

void Datapath::RemoveIPv6HostRoute(const net_base::IPv6CIDR& ipv6_cidr) {
  process_runner_->ip6("route", "del", {ipv6_cidr.ToString()});
}

bool Datapath::AddIPv6NeighborProxy(const std::string& ifname,
                                    const net_base::IPv6Address& ipv6_addr) {
  return process_runner_->ip6("neighbor", "add",
                              {"proxy", ipv6_addr.ToString(), "dev", ifname}) ==
         0;
}

void Datapath::RemoveIPv6NeighborProxy(const std::string& ifname,
                                       const net_base::IPv6Address& ipv6_addr) {
  process_runner_->ip6("neighbor", "del",
                       {"proxy", ipv6_addr.ToString(), "dev", ifname});
}

bool Datapath::AddIPv6Address(const std::string& ifname,
                              const std::string& ipv6_addr) {
  return process_runner_->ip6("addr", "add", {ipv6_addr, "dev", ifname}) == 0;
}

void Datapath::RemoveIPv6Address(const std::string& ifname,
                                 const std::string& ipv6_addr) {
  process_runner_->ip6("addr", "del", {ipv6_addr, "dev", ifname});
}

void Datapath::StartConnectionPinning(const ShillClient::Device& shill_device) {
  const std::string& ext_ifname = shill_device.ifname;
  int ifindex = system_->IfNametoindex(ext_ifname);
  if (ifindex == 0) {
    // Can happen if the interface has already been removed (b/183679000).
    LOG(ERROR) << "Failed to set up connection pinning on " << ext_ifname;
    return;
  }

  std::string subchain = "POSTROUTING_" + ext_ifname;
  // This can fail if patchpanel did not stopped correctly or failed to cleanup
  // the chain when |ext_ifname| was previously deleted.
  if (!AddChain(IpFamily::kDual, Iptables::Table::kMangle, subchain)) {
    LOG(ERROR) << "Failed to create mangle chain " << subchain;
  }
  // Make sure the chain is empty if patchpanel did not cleaned correctly that
  // chain before.
  if (!FlushChain(IpFamily::kDual, Iptables::Table::kMangle, subchain)) {
    LOG(ERROR) << "Could not flush " << subchain;
  }
  if (!ModifyJumpRule(IpFamily::kDual, Iptables::Table::kMangle,
                      Iptables::Command::kA, "POSTROUTING", subchain,
                      /*iif=*/"", ext_ifname)) {
    LOG(ERROR) << "Could not add jump rule from mangle POSTROUTING to "
               << subchain;
  }

  auto routing_mark = Fwmark::FromIfIndex(ifindex);
  if (!routing_mark.has_value()) {
    LOG(ERROR) << "Failed to compute fwmark value of interface " << ext_ifname
               << " with index " << ifindex;
    return;
  }
  LOG(INFO) << "Start connection pinning on " << ext_ifname
            << " fwmark=" << routing_mark.value().ToString();
  // Set in CONNMARK the routing tag associated with |ext_ifname|.
  if (!ModifyConnmarkSet(IpFamily::kDual, subchain, Iptables::Command::kA,
                         routing_mark.value(), kFwmarkRoutingMask)) {
    LOG(ERROR) << "Could not start connection pinning on " << ext_ifname;
  }
  // Save in CONNMARK the source tag for egress traffic of this connection.
  if (!ModifyConnmarkSave(IpFamily::kDual, subchain, Iptables::Command::kA,
                          kFwmarkAllSourcesMask)) {
    LOG(ERROR) << "Failed to add POSTROUTING CONNMARK rule for saving fwmark "
                  "source tag on "
               << ext_ifname;
  }
  // Restore from CONNMARK the source tag for ingress traffic of this connection
  // (returned traffic).
  if (!ModifyConnmarkRestore(IpFamily::kDual, "PREROUTING",
                             Iptables::Command::kA, ext_ifname,
                             kFwmarkAllSourcesMask)) {
    LOG(ERROR) << "Could not setup fwmark source tagging rule for return "
                  "traffic received on "
               << ext_ifname;
  }
}

void Datapath::StopConnectionPinning(const ShillClient::Device& shill_device) {
  const std::string& ext_ifname = shill_device.ifname;
  std::string subchain = "POSTROUTING_" + ext_ifname;
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kMangle,
                 Iptables::Command::kD, "POSTROUTING", subchain, /*iif=*/"",
                 ext_ifname);
  FlushChain(IpFamily::kDual, Iptables::Table::kMangle, subchain);
  RemoveChain(IpFamily::kDual, Iptables::Table::kMangle, subchain);
  if (!ModifyConnmarkRestore(IpFamily::kDual, "PREROUTING",
                             Iptables::Command::kD, ext_ifname,
                             kFwmarkAllSourcesMask)) {
    LOG(ERROR) << "Could not remove fwmark source tagging rule for return "
                  "traffic received on "
               << ext_ifname;
  }
}

void Datapath::StartVpnRouting(const ShillClient::Device& vpn_device) {
  const std::string& vpn_ifname = vpn_device.ifname;
  int ifindex = system_->IfNametoindex(vpn_ifname);
  if (ifindex == 0) {
    // Can happen if the interface has already been removed (b/183679000).
    LOG(ERROR) << "Failed to start VPN routing on " << vpn_ifname;
    return;
  }

  auto routing_mark = Fwmark::FromIfIndex(ifindex);
  if (!routing_mark.has_value()) {
    LOG(ERROR) << "Failed to compute fwmark value of interface " << vpn_ifname
               << " with index " << ifindex;
    return;
  }
  LOG(INFO) << "Start VPN routing on " << vpn_ifname
            << " fwmark=" << routing_mark.value().ToString();
  if (!ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kNat,
                      Iptables::Command::kA, "POSTROUTING", "MASQUERADE",
                      /*iif=*/"", vpn_ifname)) {
    LOG(ERROR) << "Could not set up SNAT for traffic outgoing " << vpn_ifname;
  }
  StartConnectionPinning(vpn_device);

  // Any traffic that already has a routing tag applied is accepted.
  if (!ModifyIptables(
          IpFamily::kDual, Iptables::Table::kMangle, Iptables::Command::kA,
          kApplyVpnMarkChain,
          {"-m", "mark", "!", "--mark", "0x0/" + kFwmarkRoutingMask.ToString(),
           "-j", "ACCEPT", "-w"})) {
    LOG(ERROR) << "Failed to add ACCEPT rule to VPN tagging chain for marked "
                  "connections";
  }
  // Otherwise, any new traffic from a new connection gets marked with the
  // VPN routing tag.
  if (!ModifyFwmarkRoutingTag(kApplyVpnMarkChain, Iptables::Command::kA,
                              routing_mark.value()))
    LOG(ERROR) << "Failed to set up VPN set-mark rule for " << vpn_ifname;

  // When the VPN client runs on the host, also route arcbr0 to that VPN so
  // that ARC can access the VPN network through arc0.
  if (vpn_ifname != kArcbr0Ifname) {
    StartRoutingDevice(vpn_device, kArcbr0Ifname, TrafficSource::kArc);
  }
  if (!ModifyRedirectDnsJumpRule(
          IpFamily::kIPv4, Iptables::Command::kA, "OUTPUT",
          /*ifname=*/"", kRedirectDnsChain, kFwmarkRouteOnVpn, kFwmarkVpnMask,
          /*redirect_on_mark=*/false)) {
    LOG(ERROR) << "Failed to set jump rule to " << kRedirectDnsChain;
  }

  // All traffic with the VPN routing tag are explicitly accepted in the filter
  // table. This prevents the VPN lockdown chain to reject that traffic when VPN
  // lockdown is enabled.
  if (!ModifyIptables(IpFamily::kDual, Iptables::Table::kFilter,
                      Iptables::Command::kA, kVpnAcceptChain,
                      {"-m", "mark", "--mark",
                       routing_mark.value().ToString() + "/" +
                           kFwmarkRoutingMask.ToString(),
                       "-j", "ACCEPT", "-w"})) {
    LOG(ERROR) << "Failed to set filter rule for accepting VPN marked traffic";
  }
}

void Datapath::StopVpnRouting(const ShillClient::Device& vpn_device) {
  const std::string& vpn_ifname = vpn_device.ifname;
  LOG(INFO) << "Stop VPN routing on " << vpn_ifname;
  if (!FlushChain(IpFamily::kDual, Iptables::Table::kFilter, kVpnAcceptChain)) {
    LOG(ERROR) << "Could not flush " << kVpnAcceptChain;
  }
  if (vpn_ifname != kArcbr0Ifname) {
    StopRoutingDevice(kArcbr0Ifname);
  }
  if (!FlushChain(IpFamily::kDual, Iptables::Table::kMangle,
                  kApplyVpnMarkChain)) {
    LOG(ERROR) << "Could not flush " << kApplyVpnMarkChain;
  }
  StopConnectionPinning(vpn_device);
  if (!ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kNat,
                      Iptables::Command::kD, "POSTROUTING", "MASQUERADE",
                      /*iif=*/"", vpn_ifname)) {
    LOG(ERROR) << "Could not stop SNAT for traffic outgoing " << vpn_ifname;
  }
  if (!ModifyRedirectDnsJumpRule(
          IpFamily::kIPv4, Iptables::Command::kD, "OUTPUT",
          /*ifname=*/"", kRedirectDnsChain, kFwmarkRouteOnVpn, kFwmarkVpnMask,
          /*redirect_on_mark=*/false)) {
    LOG(ERROR) << "Failed to remove jump rule to " << kRedirectDnsChain;
  }
}

void Datapath::SetVpnLockdown(bool enable_vpn_lockdown) {
  if (enable_vpn_lockdown) {
    if (!ModifyIptables(
            IpFamily::kDual, Iptables::Table::kFilter, Iptables::Command::kA,
            kVpnLockdownChain,
            {"-m", "mark", "--mark",
             kFwmarkRouteOnVpn.ToString() + "/" + kFwmarkVpnMask.ToString(),
             "-j", "REJECT", "-w"})) {
      LOG(ERROR) << "Failed to start VPN lockdown mode";
    }
  } else {
    if (!FlushChain(IpFamily::kDual, Iptables::Table::kFilter,
                    kVpnLockdownChain)) {
      LOG(ERROR) << "Failed to stop VPN lockdown mode";
    }
  }
}

void Datapath::StartSourceIPv6PrefixEnforcement(
    const ShillClient::Device& shill_device) {
  VLOG(2) << __func__ << ": " << shill_device;
  std::string subchain = EgressSubChainName(shill_device.ifname);
  if (!AddChain(IpFamily::kIPv6, Iptables::Table::kFilter, subchain)) {
    LOG(ERROR) << __func__ << ": Failed to create chain " << subchain;
    return;
  }
  if (!ModifyJumpRule(IpFamily::kIPv6, Iptables::Table::kFilter,
                      Iptables::Command::kI, "OUTPUT", subchain,
                      /*iif=*/"", /*oif=*/shill_device.ifname)) {
    LOG(ERROR) << __func__ << ": Failed to add jump rule from OUTPUT to "
               << subchain;
    return;
  }
  // By default, immediately start jumping to "enforce_ipv6_src_prefix" to drop
  // traffic until the prefix RETURN rule is installed.
  UpdateSourceEnforcementIPv6Prefix(shill_device, std::nullopt);
}

void Datapath::StopSourceIPv6PrefixEnforcement(
    const ShillClient::Device& shill_device) {
  VLOG(2) << __func__ << ": " << shill_device;
  std::string subchain = EgressSubChainName(shill_device.ifname);
  if (!FlushChain(IpFamily::kIPv6, Iptables::Table::kFilter, subchain)) {
    LOG(ERROR) << __func__ << ": Failed to flush " << subchain;
  }
  if (!ModifyJumpRule(IpFamily::kIPv6, Iptables::Table::kFilter,
                      Iptables::Command::kD, "OUTPUT", subchain,
                      /*iif=*/"", /*oif=*/shill_device.ifname)) {
    LOG(ERROR) << __func__ << ": Failed to remove jump rule from OUTPUT to "
               << subchain;
    return;
  }
  if (!RemoveChain(IpFamily::kIPv6, Iptables::Table::kFilter, subchain)) {
    LOG(ERROR) << __func__ << ": Failed to remove chain " << subchain;
  }
}

void Datapath::UpdateSourceEnforcementIPv6Prefix(
    const ShillClient::Device& shill_device,
    const std::optional<net_base::IPv6CIDR>& prefix) {
  VLOG(2) << __func__ << ": " << shill_device << ", {"
          << (prefix ? prefix->ToString() : "") << "}";
  std::string subchain = EgressSubChainName(shill_device.ifname);
  if (!FlushChain(IpFamily::kIPv6, Iptables::Table::kFilter, subchain)) {
    LOG(ERROR) << __func__ << ": Failed to flush " << subchain;
  }
  if (prefix) {
    if (!ModifyIptables(IpFamily::kIPv6, Iptables::Table::kFilter,
                        Iptables::Command::kA, subchain,
                        {"-s", prefix->ToString(), "-j", "RETURN", "-w"})) {
      LOG(ERROR) << __func__
                 << ": Failed to add " + prefix->ToString() + " RETURN rule in "
                 << subchain;
    }
  }
  if (!ModifyJumpRule(IpFamily::kIPv6, Iptables::Table::kFilter,
                      Iptables::Command::kA, subchain,
                      kEnforceSourcePrefixChain,
                      /*iif=*/"", /*oif=*/"")) {
    LOG(ERROR) << __func__ << ": Failed to add jump rule from " << subchain
               << " to " << kEnforceSourcePrefixChain;
  }
}

bool Datapath::StartDownstreamNetwork(const DownstreamNetworkInfo& info) {
  if (info.topology == DownstreamNetworkTopology::kLocalOnly) {
    LOG(ERROR) << __func__ << " " << info << ": LocalOnly is not supported";
    return false;
  }

  if (!info.upstream_device) {
    LOG(ERROR) << __func__ << " " << info << ": no upstream Device defined";
    return false;
  }

  // TODO(b/239559602) Clarify which service, shill or networking, is in charge
  // of IFF_UP and MAC address configuration.
  if (!ConfigureInterface(info.downstream_ifname, /*mac_addr=*/std::nullopt,
                          info.ipv4_cidr,
                          /*ipv6_cidr=*/std::nullopt,
                          /*up=*/true,
                          /*enable_multicast=*/true)) {
    LOG(ERROR) << __func__ << " " << info
               << ": Cannot configure downstream interface "
               << info.downstream_ifname;
    return false;
  }

  // Ensure any prior tethering iptables setup that might not have been
  // properly torn down is removed.
  if (!FlushChain(IpFamily::kDual, Iptables::Table::kFilter,
                  kAcceptDownstreamNetworkChain)) {
    LOG(ERROR) << __func__ << " " << info << ": Failed to flush "
               << kAcceptDownstreamNetworkChain;
  }

  // Accept DHCP traffic if DHCP is used.
  if (info.enable_ipv4_dhcp &&
      !ModifyIptables(IpFamily::kIPv4, Iptables::Table::kFilter,
                      Iptables::Command::kI, kAcceptDownstreamNetworkChain,
                      {"-p", "udp", "--dport", "67", "--sport", "68", "-j",
                       "ACCEPT", "-w"})) {
    LOG(ERROR) << "Failed to create ACCEPT rule for DHCP traffic on "
               << kAcceptDownstreamNetworkChain;
    return false;
  }

  if (!ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                      Iptables::Command::kI, "INPUT",
                      kAcceptDownstreamNetworkChain,
                      /*iif=*/info.downstream_ifname, /*oif=*/"")) {
    LOG(ERROR) << __func__ << " " << info
               << ": Failed to create jump rule from INPUT to "
               << kAcceptDownstreamNetworkChain << " for ingress traffic on "
               << info.downstream_ifname;
    return false;
  }

  // int_ipv4_addr is not necessary if route_on_vpn == false
  const auto source = DownstreamNetworkInfoTrafficSource(info);
  StartRoutingDevice(*info.upstream_device, info.downstream_ifname, source);
  return true;
}

void Datapath::StopDownstreamNetwork(const DownstreamNetworkInfo& info) {
  if (info.topology == DownstreamNetworkTopology::kLocalOnly) {
    LOG(ERROR) << __func__ << " " << info << ": LocalOnly is not supported";
    return;
  }

  // Skip unconfiguring the downstream interface: shill will either destroy it
  // or flip it back to client mode and restart a Network on top.
  StopRoutingDevice(info.downstream_ifname);
  FlushChain(IpFamily::kDual, Iptables::Table::kFilter,
             kAcceptDownstreamNetworkChain);
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                 Iptables::Command::kD, "INPUT", kAcceptDownstreamNetworkChain,
                 /*iif=*/info.downstream_ifname, /*oif=*/"");
}

bool Datapath::ModifyConnmarkSet(IpFamily family,
                                 const std::string& chain,
                                 Iptables::Command op,
                                 Fwmark mark,
                                 Fwmark mask) {
  return ModifyIptables(family, Iptables::Table::kMangle, op, chain,
                        {"-j", "CONNMARK", "--set-mark",
                         mark.ToString() + "/" + mask.ToString(), "-w"});
}

bool Datapath::ModifyConnmarkRestore(IpFamily family,
                                     const std::string& chain,
                                     Iptables::Command op,
                                     const std::string& iif,
                                     Fwmark mask) {
  std::vector<std::string> args;
  if (!iif.empty()) {
    args.insert(args.end(), {"-i", iif});
  }
  args.insert(args.end(), {"-j", "CONNMARK", "--restore-mark", "--mask",
                           mask.ToString(), "-w"});
  return ModifyIptables(family, Iptables::Table::kMangle, op, chain, args);
}

bool Datapath::ModifyConnmarkSave(IpFamily family,
                                  const std::string& chain,
                                  Iptables::Command op,
                                  Fwmark mask) {
  std::vector<std::string> args = {"-j",     "CONNMARK",      "--save-mark",
                                   "--mask", mask.ToString(), "-w"};
  return ModifyIptables(family, Iptables::Table::kMangle, op, chain, args);
}

bool Datapath::ModifyFwmarkRoutingTag(const std::string& chain,
                                      Iptables::Command op,
                                      Fwmark routing_mark) {
  return ModifyFwmark(IpFamily::kDual, chain, op, /*int_ifname=*/"",
                      /*uid_name=*/"", /*classid=*/0, routing_mark,
                      kFwmarkRoutingMask);
}

bool Datapath::ModifyFwmarkSourceTag(const std::string& chain,
                                     Iptables::Command op,
                                     TrafficSource source) {
  return ModifyFwmark(IpFamily::kDual, chain, op, /*iif=*/"", /*uid_name=*/"",
                      /*classid=*/0, Fwmark::FromSource(source),
                      kFwmarkAllSourcesMask);
}

bool Datapath::ModifyFwmarkDefaultLocalSourceTag(Iptables::Command op,
                                                 TrafficSource source) {
  std::vector<std::string> args = {"-m",
                                   "mark",
                                   "--mark",
                                   "0x0/" + kFwmarkAllSourcesMask.ToString(),
                                   "-j",
                                   "MARK",
                                   "--set-mark",
                                   Fwmark::FromSource(source).ToString() + "/" +
                                       kFwmarkAllSourcesMask.ToString(),
                                   "-w"};
  return ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                        Iptables::Command::kA, kApplyLocalSourceMarkChain,
                        args);
}

bool Datapath::ModifyFwmarkLocalSourceTag(Iptables::Command op,
                                          const LocalSourceSpecs& source) {
  if (std::string(source.uid_name).empty() && source.classid == 0)
    return false;

  Fwmark mark = Fwmark::FromSource(source.source_type);
  if (source.is_on_vpn)
    mark = mark | kFwmarkRouteOnVpn;

  return ModifyFwmark(IpFamily::kDual, kApplyLocalSourceMarkChain, op,
                      /*iif=*/"", source.uid_name, source.classid, mark,
                      kFwmarkPolicyMask);
}

bool Datapath::ModifyFwmark(IpFamily family,
                            const std::string& chain,
                            Iptables::Command op,
                            const std::string& iif,
                            const std::string& uid_name,
                            uint32_t classid,
                            Fwmark mark,
                            Fwmark mask,
                            bool log_failures) {
  std::vector<std::string> args;
  if (!iif.empty()) {
    args.insert(args.end(), {"-i", iif});
  }
  if (!uid_name.empty()) {
    args.insert(args.end(), {"-m", "owner", "--uid-owner", uid_name});
  }
  if (classid != 0) {
    args.insert(args.end(), {"-m", "cgroup", "--cgroup",
                             base::StringPrintf("0x%08x", classid)});
  }
  args.insert(args.end(),
              {"-j", "MARK", "--set-mark",
               base::StrCat({mark.ToString(), "/", mask.ToString()}), "-w"});

  return ModifyIptables(family, Iptables::Table::kMangle, op, chain, args,
                        log_failures);
}

bool Datapath::ModifyJumpRule(IpFamily family,
                              Iptables::Table table,
                              Iptables::Command op,
                              const std::string& chain,
                              const std::string& target,
                              const std::string& iif,
                              const std::string& oif,
                              bool log_failures) {
  std::vector<std::string> args;
  if (!iif.empty()) {
    args.insert(args.end(), {"-i", iif});
  }
  if (!oif.empty()) {
    args.insert(args.end(), {"-o", oif});
  }
  args.insert(args.end(), {"-j", target, "-w"});
  return ModifyIptables(family, table, op, chain, args, log_failures);
}

bool Datapath::ModifyFwmarkVpnJumpRule(const std::string& chain,
                                       Iptables::Command op,
                                       Fwmark mark,
                                       Fwmark mask) {
  std::vector<std::string> args;
  if (mark.Value() != 0 && mask.Value() != 0) {
    args.insert(args.end(),
                {"-m", "mark", "--mark",
                 base::StrCat({mark.ToString(), "/", mask.ToString()})});
  }
  args.insert(args.end(), {"-j", kApplyVpnMarkChain, "-w"});
  return ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, op, chain,
                        args);
}

bool Datapath::ModifyFwmarkSkipVpnJumpRule(const std::string& chain,
                                           Iptables::Command op,
                                           const std::string& uid,
                                           bool log_failures) {
  std::vector<std::string> args;
  if (!uid.empty()) {
    args.insert(args.end(), {"-m", "owner", "!", "--uid-owner", uid});
  }
  args.insert(args.end(), {"-j", kSkipApplyVpnMarkChain, "-w"});
  return ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, op, chain,
                        args, log_failures);
}

bool Datapath::CheckChain(IpFamily family,
                          Iptables::Table table,
                          const std::string& name) {
  return ModifyChain(family, table, Iptables::Command::kC, name,
                     /*log_failures=*/false);
}

bool Datapath::AddChain(IpFamily family,
                        Iptables::Table table,
                        const std::string& chain) {
  DCHECK(chain.size() <= kIptablesMaxChainLength)
      << "chain name " << chain << " is longer than "
      << kIptablesMaxChainLength;
  return ModifyChain(family, table, Iptables::Command::kN, chain);
}

bool Datapath::RemoveChain(IpFamily family,
                           Iptables::Table table,
                           const std::string& chain) {
  return ModifyChain(family, table, Iptables::Command::kX, chain);
}

bool Datapath::FlushChain(IpFamily family,
                          Iptables::Table table,
                          const std::string& chain) {
  return ModifyChain(family, table, Iptables::Command::kF, chain);
}

bool Datapath::ModifyChain(IpFamily family,
                           Iptables::Table table,
                           Iptables::Command command,
                           std::string_view chain,
                           bool log_failures) {
  return ModifyIptables(family, table, command, chain, {"-w"}, log_failures);
}

bool Datapath::ModifyIptables(IpFamily family,
                              Iptables::Table table,
                              Iptables::Command command,
                              std::string_view chain,
                              const std::vector<std::string>& argv,
                              bool log_failures,
                              std::optional<base::TimeDelta> timeout) {
  bool success = true;
  if (family == IpFamily::kIPv4 || family == IpFamily::kDual) {
    success &= process_runner_->iptables(table, command, chain, argv,
                                         log_failures, timeout) == 0;
  }
  if (family == IpFamily::kIPv6 || family == IpFamily::kDual) {
    success &= process_runner_->ip6tables(table, command, chain, argv,
                                          log_failures, timeout) == 0;
  }
  return success;
}

std::string Datapath::DumpIptables(IpFamily family, Iptables::Table table) {
  std::string result;
  std::vector<std::string> argv = {"-x", "-v", "-n", "-w"};
  switch (family) {
    case IpFamily::kIPv4:
      if (process_runner_->iptables(
              table, Iptables::Command::kL, /*chain=*/"", argv,
              /*log_failures=*/true, /*timeout=*/std::nullopt, &result) != 0) {
        LOG(ERROR) << "Could not dump iptables " << table;
      }
      break;
    case IpFamily::kIPv6:
      if (process_runner_->ip6tables(
              table, Iptables::Command::kL, /*chain=*/"", argv,
              /*log_failures=*/true, /*timeout=*/std::nullopt, &result) != 0) {
        LOG(ERROR) << "Could not dump ip6tables " << table;
      }
      break;
    case IpFamily::kDual:
      LOG(ERROR) << "Cannot dump iptables and ip6tables at the same time";
      break;
  }
  return result;
}

bool Datapath::AddIPv4RouteToTable(const std::string& ifname,
                                   const net_base::IPv4CIDR& ipv4_cidr,
                                   int table_id) {
  return process_runner_->ip("route", "add",
                             {ipv4_cidr.ToString(), "dev", ifname, "table",
                              base::NumberToString(table_id)}) == 0;
}

void Datapath::DeleteIPv4RouteFromTable(const std::string& ifname,
                                        const net_base::IPv4CIDR& ipv4_cidr,
                                        int table_id) {
  process_runner_->ip("route", "del",
                      {ipv4_cidr.ToString(), "dev", ifname, "table",
                       base::NumberToString(table_id)});
}

bool Datapath::AddIPv4Route(const IPv4Address& gateway_addr,
                            const IPv4CIDR& subnet_cidr) {
  struct rtentry route;
  memset(&route, 0, sizeof(route));
  SetSockaddrIn(&route.rt_gateway, gateway_addr);
  SetSockaddrIn(&route.rt_dst, subnet_cidr.GetPrefixCIDR().address());
  SetSockaddrIn(&route.rt_genmask, subnet_cidr.ToNetmask());
  route.rt_flags = RTF_UP | RTF_GATEWAY;
  return ModifyIPv4Rtentry(SIOCADDRT, &route);
}

bool Datapath::DeleteIPv4Route(const IPv4Address& gateway_addr,
                               const IPv4CIDR& subnet_cidr) {
  struct rtentry route;
  memset(&route, 0, sizeof(route));
  SetSockaddrIn(&route.rt_gateway, gateway_addr);
  SetSockaddrIn(&route.rt_dst, subnet_cidr.GetPrefixCIDR().address());
  SetSockaddrIn(&route.rt_genmask, subnet_cidr.ToNetmask());
  route.rt_flags = RTF_UP | RTF_GATEWAY;
  return ModifyIPv4Rtentry(SIOCDELRT, &route);
}

bool Datapath::AddIPv6Route(const IPv6Address& gateway_addr,
                            const IPv6CIDR& subnet_cidr) {
  struct in6_rtmsg route;
  memset(&route, 0, sizeof(route));
  route.rtmsg_gateway = gateway_addr.ToIn6Addr();
  route.rtmsg_dst = subnet_cidr.GetPrefixCIDR().address().ToIn6Addr();
  route.rtmsg_dst_len = static_cast<uint16_t>(subnet_cidr.prefix_length());
  route.rtmsg_flags = RTF_UP | RTF_GATEWAY;
  return ModifyIPv6Rtentry(SIOCADDRT, &route);
}

bool Datapath::DeleteIPv6Route(const IPv6Address& gateway_addr,
                               const IPv6CIDR& subnet_cidr) {
  struct in6_rtmsg route;
  memset(&route, 0, sizeof(route));
  route.rtmsg_gateway = gateway_addr.ToIn6Addr();
  route.rtmsg_dst = subnet_cidr.GetPrefixCIDR().address().ToIn6Addr();
  route.rtmsg_dst_len = static_cast<uint16_t>(subnet_cidr.prefix_length());
  route.rtmsg_flags = RTF_UP | RTF_GATEWAY;
  return ModifyIPv6Rtentry(SIOCDELRT, &route);
}

bool Datapath::ModifyIPv4Rtentry(ioctl_req_t op, struct rtentry* route) {
  DCHECK(route);
  if (op != SIOCADDRT && op != SIOCDELRT) {
    LOG(ERROR) << "Invalid operation " << op << " for rtentry " << *route;
    return false;
  }
  base::ScopedFD fd(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to create socket for adding rtentry " << *route;
    return false;
  }
  if (HANDLE_EINTR(system_->Ioctl(fd.get(), op, route)) != 0) {
    // b/190119762: Ignore "No such process" errors when deleting a struct
    // rtentry if some other prior or concurrent operation already resulted in
    // this route being deleted.
    if (op == SIOCDELRT && errno == ESRCH) {
      return true;
    }
    std::string opname = op == SIOCADDRT ? "add" : "delete";
    PLOG(ERROR) << "Failed to " << opname << " rtentry " << *route;
    return false;
  }
  return true;
}

bool Datapath::ModifyIPv6Rtentry(ioctl_req_t op, struct in6_rtmsg* route) {
  DCHECK(route);
  if (op != SIOCADDRT && op != SIOCDELRT) {
    LOG(ERROR) << "Invalid operation " << op << " for rtentry " << *route;
    return false;
  }
  base::ScopedFD fd(socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to create socket for adding rtentry " << *route;
    return false;
  }
  if (HANDLE_EINTR(system_->Ioctl(fd.get(), op, route)) != 0) {
    // b/190119762: Ignore "No such process" errors when deleting a struct
    // rtentry if some other prior or concurrent operation already resulted in
    // this route being deleted.
    if (op == SIOCDELRT && errno == ESRCH) {
      return true;
    }
    std::string opname = op == SIOCADDRT ? "add" : "delete";
    PLOG(ERROR) << "Failed to " << opname << " rtentry " << *route;
    return false;
  }
  return true;
}

bool Datapath::AddAdbPortForwardRule(const std::string& ifname) {
  return firewall_->AddIpv4ForwardRule(patchpanel::ModifyPortRuleRequest::TCP,
                                       kArcAddr, kAdbServerPort, ifname,
                                       kLocalhostAddr, kAdbProxyTcpListenPort);
}

void Datapath::DeleteAdbPortForwardRule(const std::string& ifname) {
  firewall_->DeleteIpv4ForwardRule(patchpanel::ModifyPortRuleRequest::TCP,
                                   kArcAddr, kAdbServerPort, ifname,
                                   kLocalhostAddr, kAdbProxyTcpListenPort);
}

bool Datapath::AddAdbPortAccessRule(const std::string& ifname) {
  return firewall_->AddAcceptRules(patchpanel::ModifyPortRuleRequest::TCP,
                                   kAdbProxyTcpListenPort, ifname);
}

void Datapath::DeleteAdbPortAccessRule(const std::string& ifname) {
  firewall_->DeleteAcceptRules(patchpanel::ModifyPortRuleRequest::TCP,
                               kAdbProxyTcpListenPort, ifname);
}

bool Datapath::SetConntrackHelpers(const bool enable_helpers) {
  return system_->SysNetSet(System::SysNet::kConntrackHelper,
                            enable_helpers ? "1" : "0");
}

bool Datapath::SetRouteLocalnet(const std::string& ifname, const bool enable) {
  return system_->SysNetSet(System::SysNet::kIPv4RouteLocalnet,
                            enable ? "1" : "0", ifname);
}

bool Datapath::ModprobeAll(const std::vector<std::string>& modules) {
  return process_runner_->modprobe_all(modules) == 0;
}

bool Datapath::ModifyPortRule(
    const patchpanel::ModifyPortRuleRequest& request) {
  switch (request.proto()) {
    case patchpanel::ModifyPortRuleRequest::TCP:
    case patchpanel::ModifyPortRuleRequest::UDP:
      break;
    default:
      LOG(ERROR) << "Unknown protocol " << request.proto();
      return false;
  }
  if (request.input_dst_port() > UINT16_MAX) {
    LOG(ERROR) << "Invalid matching destination port "
               << request.input_dst_port();
    return false;
  }
  if (request.dst_port() > UINT16_MAX) {
    LOG(ERROR) << "Invalid forwarding destination port " << request.dst_port();
    return false;
  }
  const auto input_dst_ip =
      net_base::IPv4Address::CreateFromString(request.input_dst_ip());
  if (!request.input_dst_ip().empty() && !input_dst_ip) {
    LOG(ERROR) << "Invalid input destination ip: " << request.input_dst_ip();
    return false;
  }
  const auto dst_ip = net_base::IPv4Address::CreateFromString(request.dst_ip());
  if (request.type() == patchpanel::ModifyPortRuleRequest::FORWARDING &&
      !dst_ip) {
    LOG(ERROR) << "Invalid forwarding destination address: "
               << request.dst_ip();
    return false;
  }
  uint16_t input_dst_port = static_cast<uint16_t>(request.input_dst_port());
  uint16_t dst_port = static_cast<uint16_t>(request.dst_port());

  switch (request.op()) {
    case patchpanel::ModifyPortRuleRequest::CREATE:
      switch (request.type()) {
        case patchpanel::ModifyPortRuleRequest::ACCESS: {
          return firewall_->AddAcceptRules(request.proto(), input_dst_port,
                                           request.input_ifname());
        }
        case patchpanel::ModifyPortRuleRequest::LOCKDOWN:
          return firewall_->AddLoopbackLockdownRules(request.proto(),
                                                     input_dst_port);
        case patchpanel::ModifyPortRuleRequest::FORWARDING:
          return firewall_->AddIpv4ForwardRule(
              request.proto(), input_dst_ip, input_dst_port,
              request.input_ifname(), *dst_ip, dst_port);
        default:
          LOG(ERROR) << "Unknown port rule type " << request.type();
          return false;
      }
    case patchpanel::ModifyPortRuleRequest::DELETE:
      switch (request.type()) {
        case patchpanel::ModifyPortRuleRequest::ACCESS:
          return firewall_->DeleteAcceptRules(request.proto(), input_dst_port,
                                              request.input_ifname());
        case patchpanel::ModifyPortRuleRequest::LOCKDOWN:
          return firewall_->DeleteLoopbackLockdownRules(request.proto(),
                                                        input_dst_port);
        case patchpanel::ModifyPortRuleRequest::FORWARDING:
          return firewall_->DeleteIpv4ForwardRule(
              request.proto(), input_dst_ip, input_dst_port,
              request.input_ifname(), *dst_ip, dst_port);
        default:
          LOG(ERROR) << "Unknown port rule type " << request.type();
          return false;
      }
    default:
      LOG(ERROR) << "Unknown operation " << request.op();
      return false;
  }
}

void Datapath::SetupQoSDetectChain() {
  const auto install_rule = [this](IpFamily family,
                                   const std::vector<std::string>& args) {
    ModifyIptables(family, Iptables::Table::kMangle, Iptables::Command::kA,
                   kQoSDetectChain, args);
  };

  const std::string qos_mask = kFwmarkQoSCategoryMask.ToString();
  const std::string default_mark = QoSFwmarkWithMask(QoSCategory::kDefault);
  const std::string network_control_mark =
      QoSFwmarkWithMask(QoSCategory::kNetworkControl);

  // Reset the QoS-related bits in fwmark. Some sockets will set their own
  // fwmarks when sending packets, while this is not compatible with the rules
  // here. See b/303216552 for an example. Note that the matcher part in this
  // rule (`--mark 0x0/0xe0`) is not a must, just for checking how many packets
  // have their own fwmarks.
  install_rule(IpFamily::kDual,
               {"-m", "mark", "!", "--mark", default_mark, "-j", "MARK",
                "--set-xmark", default_mark, "-w"});

  // Skip QoS detection if DSCP value is already set.
  install_rule(IpFamily::kDual,
               {"-m", "dscp", "!", "--dscp", "0", "-j", "RETURN", "-w"});

  // Restore the QoS bits from the conntrack mark to the fwmark of a packet.
  // This is used by connections detected by ARC++ socket monitor and WebRTC
  // detector. This will override the original fwmark on the packet (if the
  // sender sets it) by intention.
  install_rule(IpFamily::kDual, {"-j", "CONNMARK", "--restore-mark", "--nfmask",
                                 qos_mask, "--ctmask", qos_mask, "-w"});

  // If the value restored from the conntrack mark is not 0, skip the following
  // detection.
  install_rule(IpFamily::kDual, {"-m", "mark", "!", "--mark", default_mark,
                                 "-j", "RETURN", "-w"});

  // Marking the first packet in the TCP handshake (SYN bit set and the ACK,RST
  // and FIN bits cleared). We only care about the TCP connection initiated from
  // the device now.
  install_rule(IpFamily::kDual, {"-p", "tcp", "--syn", "-j", "MARK",
                                 "--set-xmark", network_control_mark, "-w"});

  // Marking ICMP packets.
  install_rule(IpFamily::kIPv4, {"-p", "icmp", "-j", "MARK", "--set-xmark",
                                 network_control_mark, "-w"});
  install_rule(IpFamily::kIPv6, {"-p", "icmpv6", "-j", "MARK", "--set-xmark",
                                 network_control_mark, "-w"});

  // Marking DNS packets. 853 for DoT for Android is ignored here since it won't
  // happen when dns-proxy is on.
  install_rule(IpFamily::kDual, {"-p", "udp", "--dport", "53", "-j", "MARK",
                                 "--set-xmark", network_control_mark, "-w"});
  install_rule(IpFamily::kDual, {"-p", "tcp", "--dport", "53", "-j", "MARK",
                                 "--set-xmark", network_control_mark, "-w"});

  // Add a jump rule to the DoH detection chain. Rules in this chain will be
  // installed dynamically in UpdateDoHProvidersForQoS().
  install_rule(IpFamily::kDual, {"-j", kQoSDetectDoHChain, "-w"});

  // TODO(b/296952085): Add rules for WebRTC detection. Also need to add an
  // early-return rule above for packets marked by rules for network control, to
  // avoid marks on TCP handshake packets being persisted into connmark.
}

void Datapath::SetupQoSApplyDSCPChain() {
  // From QoS categories to DSCP values. See go/cros-qos-dscp-classes-1p for the
  // mapping.
  constexpr struct {
    QoSCategory category;
    std::string_view dscp;
  } kDSCPApplyRules[] = {
      {QoSCategory::kRealTimeInteractive, "32"},
      {QoSCategory::kMultimediaConferencing, "34"},
      {QoSCategory::kNetworkControl, "48"},
      {QoSCategory::kWebRTC, "34"},
  };
  for (const auto& rule : kDSCPApplyRules) {
    ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                   Iptables::Command::kA, kQoSApplyDSCPChain,
                   {"-m", "mark", "--mark", QoSFwmarkWithMask(rule.category),
                    "-j", "DSCP", "--set-dscp", std::string(rule.dscp), "-w"});
  }
}

void Datapath::EnableQoSDetection() {
  ModifyQoSDetectJumpRule(Iptables::Command::kA);
}

void Datapath::DisableQoSDetection() {
  ModifyQoSDetectJumpRule(Iptables::Command::kD);
}

void Datapath::EnableQoSApplyingDSCP(std::string_view ifname) {
  LOG(INFO) << "Enable QoS DSCP application on " << ifname;
  ModifyQoSApplyDSCPJumpRule(Iptables::Command::kA, ifname);
}

void Datapath::DisableQoSApplyingDSCP(std::string_view ifname) {
  LOG(INFO) << "Disable QoS DSCP application on " << ifname;
  ModifyQoSApplyDSCPJumpRule(Iptables::Command::kD, ifname);
}

void Datapath::ModifyQoSDetectJumpRule(Iptables::Command command) {
  ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, command,
                 kQoSDetectStaticChain, {"-j", kQoSDetectChain, "-w"});
}

void Datapath::ModifyQoSApplyDSCPJumpRule(Iptables::Command command,
                                          std::string_view ifname) {
  ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, command,
                 "POSTROUTING",
                 {"-o", std::string(ifname), "-j", kQoSApplyDSCPChain, "-w"});
}

void Datapath::UpdateDoHProvidersForQoS(
    IpFamily family, const std::vector<net_base::IPAddress>& doh_provider_ips) {
  // Clear all the rules for the previous DoH providers.
  FlushChain(family, Iptables::Table::kMangle, kQoSDetectDoHChain);

  if (doh_provider_ips.empty()) {
    return;
  }

  // Get the string format of the IP addresses.
  std::vector<std::string> ip_strs;
  ip_strs.reserve(doh_provider_ips.size());
  for (const auto& ip : doh_provider_ips) {
    ip_strs.push_back(ip.ToString());
  }

  // Mark all the TCP and UDP traffic to the 443 port of the DoH servers. This
  // may have false positives if the server is also used for non-DNS HTTPS
  // traffic.
  for (std::string_view protocol : {"udp", "tcp"}) {
    ModifyIptables(family, Iptables::Table::kMangle, Iptables::Command::kA,
                   kQoSDetectDoHChain,
                   {"-p", std::string(protocol), "--dport", "443", "-d",
                    base::JoinString(ip_strs, ","), "-j", "MARK", "--set-xmark",
                    QoSFwmarkWithMask(QoSCategory::kNetworkControl), "-w"});
  }
}

bool Datapath::ModifyClatAcceptRules(Iptables::Command command,
                                     const std::string& ifname) {
  bool success = true;
  success &= ModifyJumpRule(IpFamily::kIPv6, Iptables::Table::kFilter, command,
                            "FORWARD", "ACCEPT", /*iif=*/ifname, /*oif=*/"");
  success &= ModifyJumpRule(IpFamily::kIPv6, Iptables::Table::kFilter, command,
                            "FORWARD", "ACCEPT", /*iif=*/"", /*oif=*/ifname);
  return success;
}

std::ostream& operator<<(std::ostream& stream,
                         const ConnectedNamespace& nsinfo) {
  stream << "{ pid: " << nsinfo.pid
         << ", source: " << TrafficSourceName(nsinfo.source);
  if (!nsinfo.outbound_ifname.empty()) {
    stream << ", outbound_ifname: " << nsinfo.outbound_ifname;
  }
  stream << ", route_on_vpn: " << nsinfo.route_on_vpn
         << ", host_ifname: " << nsinfo.host_ifname
         << ", peer_ifname: " << nsinfo.peer_ifname
         << ", peer_ipv4_subnet: " << nsinfo.peer_ipv4_subnet->base_cidr();
  if (nsinfo.static_ipv6_config.has_value()) {
    stream << ", static_ipv6_subnet: "
           << nsinfo.static_ipv6_config->host_cidr.GetPrefixCIDR();
  }
  stream << '}';
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const DnsRedirectionRule& rule) {
  stream << "{ type: "
         << SetDnsRedirectionRuleRequest::RuleType_Name(rule.type);
  if (!rule.input_ifname.empty()) {
    stream << ", input_ifname: " << rule.input_ifname;
  }
  stream << ", proxy_address: " << rule.proxy_address;
  if (!rule.nameservers.empty()) {
    stream << ", nameserver(s): ";
    for (const auto& nameserver : rule.nameservers) {
      stream << nameserver << ",";
    }
  }
  stream << " }";
  return stream;
}

}  // namespace patchpanel
