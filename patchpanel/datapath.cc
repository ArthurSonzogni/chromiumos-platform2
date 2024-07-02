// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/datapath.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <linux/sockios.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/userdb_utils.h>
#include <chromeos/net-base/mac_address.h>

#include "patchpanel/adb_proxy.h"
#include "patchpanel/arc_service.h"
#include "patchpanel/bpf/constants.h"
#include "patchpanel/iptables.h"
#include "patchpanel/net_util.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/scoped_ns.h"

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
constexpr uint16_t kAdbServerPort = 5555;

constexpr std::string_view kIptablesStartScriptPath =
    "/etc/patchpanel/iptables.start";
constexpr std::string_view kIp6tablesStartScriptPath =
    "/etc/patchpanel/ip6tables.start";

// Chains for tagging egress traffic in the OUTPUT and PREROUTING chains of the
// mangle table. Note that these value needs to be consistent with those in the
// static iptables-start scripts.
constexpr char kSkipApplyVpnMarkChain[] = "skip_apply_vpn_mark";
constexpr char kApplyVpnMarkChain[] = "apply_vpn_mark";

// Egress filter chain to allow traffic to DNS proxy.
constexpr char kAcceptEgressToDnsProxyChain[] = "accept_egress_to_dns_proxy";

// Egress filter chain for dropping in the OUTPUT chain any local traffic
// incorrectly bound to a static IPv4 address used for ARC or Crostini.
constexpr char kDropGuestIpv4PrefixChain[] = "drop_guest_ipv4_prefix";

// Egress nat chain for redirecting DNS queries from system services.
// TODO(b/162788331) Remove once dns-proxy has become fully operational.
constexpr char kRedirectDnsChain[] = "redirect_dns";

// OUTPUT filter chain to enforce source IP on egress IPv6 packets.
constexpr char kEnforceSourcePrefixChain[] = "enforce_ipv6_src_prefix";

// VPN egress filter chains for the filter OUTPUT and FORWARD chains.
constexpr char kVpnAcceptChain[] = "vpn_accept";
constexpr char kVpnLockdownChain[] = "vpn_lockdown";

// FORWARD filter chain to:
//  - accept any tethering traffic forwarded between the upstream and downstream
//  network interfaces.
//  - and drop any forwarded traffic between the downstream network interface
//  and some other network interface unrelated to tethering.
// Note that the upstream network interface may be used for other forwarded
// traffic legitimately.
constexpr char kForwardTetheringChain[] = "forward_tethering";
// OUPUT filter chain to accept egress traffic sent by ChromeOS on the
// downstream network interface of a tethered connection.
constexpr char kEgressTetheringChain[] = "egress_tethering";
// INPUT filter chain to accept ingress traffic received by ChromeOS on the
// downstream network interface of a tethered connection by a tethering client.
constexpr char kIngressTetheringChain[] = "ingress_tethering";

// FORWARD filter chain to stop any forwarded traffic between a downstream
// network interface and any other physical network, VPN, or guest virtual
// network.
constexpr char kForwardLocalOnlyChain[] = "forward_localonly";
// OUPUT filter chain to accept egress traffic sent by ChromeOS on the
// downstream network interface of a local only network managed by patchpanel.
constexpr char kEgressLocalOnlyChain[] = "egress_localonly";
// INPUT filter chain to accept ingress traffic received by ChromeOS on the
// downstream network interface of a local only network managed by patchpanel.
constexpr char kIngressLocalOnlyChain[] = "ingress_localonly";

// INPUT filter chain to jump to the specialized ingress chains for tethering or
// local only networks. This static chain ensures the correct the traversal
// orders of other INPUT rules and must be after "ingress_port_firewall".
constexpr char kIngressDownstreamNetworkChain[] = "ingress_downstream_network";

// OUTPUT filter chain to drop host-initiated connection to Bruschetta and
// FORWARD filter chain to drop external- and other-vm-initiated connection.
constexpr char kDropOutputToBruschettaChain[] = "drop_output_to_bruschetta";
constexpr char kDropForwardToBruschettaChain[] = "drop_forward_to_bruschetta";

// IPv4 nat PREROUTING chains for forwarding ingress traffic to different types
// of hosted guests with the corresponding hierarchy.
constexpr char kApplyAutoDNATToArcChain[] = "apply_auto_dnat_to_arc";
constexpr char kApplyAutoDNATToCrostiniChain[] = "apply_auto_dnat_to_crostini";
constexpr char kApplyAutoDNATToParallelsChain[] =
    "apply_auto_dnat_to_parallels";
// nat PREROUTING chain for egress traffic from downstream guests.
constexpr char kRedirectDefaultDnsChain[] = "redirect_default_dns";
// nat OUTPUT chain for egress traffic from processes running on the host.
constexpr char kRedirectUserDnsChain[] = "redirect_user_dns";
// nat POSTROUTING chain for egress traffic from processes running on the host.
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
// mangle chain for holding the dynamic matching rules for Borealis. Referenced
// in the qos_detect chain.
constexpr char kQoSDetectBorealisChain[] = "qos_detect_borealis";
// mangle POSTROUTING chain for applying DSCP fields based on fwmarks for egress
// traffic.
constexpr char kQoSApplyDSCPChain[] = "qos_apply_dscp";

// Maximum length of an iptables chain name.
constexpr int kIptablesMaxChainLength = 28;

IpFamily ConvertIpFamily(net_base::IPFamily family) {
  return (family == net_base::IPFamily::kIPv4) ? IpFamily::kIPv4
                                               : IpFamily::kIPv6;
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
std::string PreroutingSubChainName(std::string_view int_ifname) {
  return base::StrCat({"PREROUTING_", int_ifname});
}

std::string EgressSubChainName(std::string_view ext_ifname) {
  return base::StrCat({"egress_", ext_ifname});
}

// Helper enum for controlling what type of FORWARD firewall rules are
// configured for a given network interface.
enum class ForwardFirewallRuleType {
  // Rules for the downstream interface used for tethering. It is assumed that:
  //  - The downstream interface is not used for anything other traffic.
  //  - There is at most a single unique tethering setup on the device.
  kTethering,
  // Rules for a downstream interface used for a local-only network associated
  // with a WiFi hotspot or a WiFi Direct Group Owner link. At the moment, no
  // traffic forwarding is allowed.
  kLocalOnly,
  // Rules for a virtual interface used for an isolated guest VM like Bruschetta
  // with strict ingress firewall rules. Only traffic originated from the VM is
  // allowed.
  kIsolatedGuest,
  // Rules for any other virtual interface or downstream network interface.
  kOpen,
};

ForwardFirewallRuleType GetForwardFirewallRuleType(TrafficSource source) {
  switch (source) {
    case TrafficSource::kTetherDownstream:
      return ForwardFirewallRuleType::kTethering;
    case TrafficSource::kWiFiLOHS:
    case TrafficSource::kWiFiDirect:
      return ForwardFirewallRuleType::kLocalOnly;
    case TrafficSource::kBruschettaVM:
      return ForwardFirewallRuleType::kIsolatedGuest;
    default:
      return ForwardFirewallRuleType::kOpen;
  }
}

std::string_view GetEgressFilterChainName(DownstreamNetworkTopology topology) {
  switch (topology) {
    case DownstreamNetworkTopology::kLocalOnly:
      return kEgressLocalOnlyChain;
    case DownstreamNetworkTopology::kTethering:
      return kEgressTetheringChain;
  }
}

std::string_view GetIngressFilterChainName(DownstreamNetworkTopology topology) {
  switch (topology) {
    case DownstreamNetworkTopology::kLocalOnly:
      return kIngressLocalOnlyChain;
    case DownstreamNetworkTopology::kTethering:
      return kIngressTetheringChain;
  }
}

bool IsDownstreamNetworkForwardFirewallRule(ForwardFirewallRuleType rule) {
  return rule == ForwardFirewallRuleType::kTethering ||
         rule == ForwardFirewallRuleType::kLocalOnly;
}

}  // namespace

Datapath::Datapath(System* system)
    : Datapath(MinijailedProcessRunner::GetInstance(), new Firewall(), system) {
}

Datapath::Datapath(MinijailedProcessRunner* process_runner,
                   Firewall* firewall,
                   System* system)
    : process_runner_(process_runner), system_(system) {
  firewall_.reset(firewall);
}

void Datapath::Start() {
  // Enable IPv4 packet forwarding
  if (!system_->SysNetSet(System::SysNet::kIPv4Forward, "1")) {
    LOG(ERROR) << "Failed to update net.ipv4.ip_forward."
               << " Guest connectivity will not work correctly.";
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

  if (process_runner_->iptables_restore(kIptablesStartScriptPath) != 0) {
    LOG(ERROR) << "Failed to call iptables_restore";
  }
  if (process_runner_->ip6tables_restore(kIp6tablesStartScriptPath) != 0) {
    LOG(ERROR) << "Failed to call ip6tables_restore";
  }

  // Rules for WebRTC detection. Notes:
  // - In short, this is implemented by detecting the client hello packet in a
  //   WebRTC connection, and mark the whole connection on success. See the
  //   WebRTC detection section in go/cros-wifi-qos-dd for more details.
  // - The BPF program will only be installed on 5.8+ kernels, where CAP_BPF is
  //   available. We check if the existence of the program instead of the kernel
  //   version here. That's why only WebRTC detection is done here dynamically
  //   while other QoS detection rules are in the static iptables-start script.
  // - Marking the whole connection is implemented by saving the mark into
  //   connmark. To avoid saving the mark for unrelated connections, we check if
  //   the packet already have a mark at first.
  if (system_->IsEbpfEnabled()) {
    auto run_iptables_in_batch = process_runner_->AcquireIptablesBatchMode();

    const auto install_rule =
        [this](IpFamily family, const std::vector<std::string_view>& args) {
          ModifyIptables(family, Iptables::Table::kMangle,
                         Iptables::Command::kA, kQoSDetectChain, args);
        };

    const std::string qos_mask = kFwmarkQoSCategoryMask.ToString();
    const std::string default_mark = QoSFwmarkWithMask(QoSCategory::kDefault);
    const std::string multimedia_conferencing_mark =
        QoSFwmarkWithMask(QoSCategory::kMultimediaConferencing);

    install_rule(IpFamily::kDual, {"-m", "mark", "!", "--mark", default_mark,
                                   "-j", "RETURN", "-w"});
    install_rule(IpFamily::kDual,
                 {"-m", "bpf", "--object-pinned", kWebRTCMatcherPinPath, "-j",
                  "MARK", "--set-xmark", multimedia_conferencing_mark, "-w"});
    install_rule(IpFamily::kDual, {"-j", "CONNMARK", "--save-mark", "--nfmask",
                                   qos_mask, "--ctmask", qos_mask, "-w"});
  }
}

void Datapath::Stop() {
  // Disable packet forwarding
  if (!system_->SysNetSet(System::SysNet::kIPv6Forward, "0")) {
    LOG(ERROR) << "Failed to restore net.ipv6.conf.all.forwarding.";
  }

  if (!system_->SysNetSet(System::SysNet::kIPv4Forward, "0")) {
    LOG(ERROR) << "Failed to restore net.ipv4.ip_forward.";
  }
}

bool Datapath::NetnsAttachName(std::string_view netns_name, pid_t netns_pid) {
  // Try first to delete any netns with name |netns_name| in case patchpanel
  // did not exit cleanly.
  if (process_runner_->ip_netns_delete(netns_name,
                                       /*log_failures=*/false) == 0)
    LOG(INFO) << "Deleted left over network namespace name " << netns_name;

  if (netns_pid == ConnectedNamespace::kNewNetnsPid)
    return process_runner_->ip_netns_add(netns_name) == 0;
  else
    return process_runner_->ip_netns_attach(netns_name, netns_pid) == 0;
}

bool Datapath::NetnsDeleteName(std::string_view netns_name) {
  return process_runner_->ip_netns_delete(netns_name) == 0;
}

bool Datapath::AddBridge(std::string_view ifname, const IPv4CIDR& cidr) {
  base::ScopedFD control_fd(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!control_fd.is_valid() ||
      system_->Ioctl(control_fd.get(), SIOCBRADDBR,
                     std::string(ifname).c_str()) != 0) {
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

void Datapath::RemoveBridge(std::string_view ifname) {
  process_runner_->ip("link", "set", {ifname, "down"});

  base::ScopedFD control_fd(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!control_fd.is_valid() ||
      system_->Ioctl(control_fd.get(), SIOCBRDELBR,
                     std::string(ifname).c_str()) != 0) {
    LOG(ERROR) << "Failed to destroy bridge " << ifname;
  }
}

bool Datapath::AddToBridge(std::string_view br_ifname,
                           std::string_view ifname) {
  struct ifreq ifr;
  FillInterfaceRequest(br_ifname, &ifr);
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
    std::string_view name,
    const std::optional<net_base::MacAddress>& mac_addr,
    const std::optional<net_base::IPv4CIDR>& ipv4_cidr,
    std::string_view user,
    DeviceMode dev_mode) {
  base::ScopedFD dev = system_->OpenTunDev();
  if (!dev.is_valid()) {
    PLOG(ERROR) << "Failed to open tun device";
    return "";
  }

  struct ifreq ifr;
  FillInterfaceRequest(name.empty() ? kDefaultIfname : name, &ifr);
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
    if (!brillo::userdb::GetUserInfo(std::string(user), &uid, nullptr)) {
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
    memcpy(&hwaddr->sa_data, mac_addr->data(),
           net_base::MacAddress::kAddressLength);
    if (system_->Ioctl(sock.get(), SIOCSIFHWADDR, &ifr) != 0) {
      PLOG(ERROR) << "Failed to set mac address for " << dev_mode
                  << " interface " << ifname << " {" << mac_addr->ToString()
                  << "}";
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

void Datapath::RemoveTunTap(std::string_view ifname, DeviceMode dev_mode) {
  std::string_view dev_mode_str =
      (dev_mode == DeviceMode::kTun) ? "tun" : "tap";
  process_runner_->ip("tuntap", "del", {ifname, "mode", dev_mode_str},
                      /*as_patchpanel_user=*/true);
}

bool Datapath::ConnectVethPair(pid_t netns_pid,
                               std::string_view netns_name,
                               std::string_view veth_ifname,
                               std::string_view peer_ifname,
                               net_base::MacAddress remote_mac_addr,
                               const IPv4CIDR& remote_ipv4_cidr,
                               const std::optional<IPv6CIDR>& remote_ipv6_cidr,
                               bool remote_multicast_flag,
                               bool up) {
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
                            remote_ipv6_cidr, up, remote_multicast_flag)) {
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

bool Datapath::AddVirtualInterfacePair(std::string_view netns_name,
                                       std::string_view veth_ifname,
                                       std::string_view peer_ifname) {
  return process_runner_->ip("link", "add",
                             {veth_ifname, "type", "veth", "peer", "name",
                              peer_ifname, "netns", netns_name}) == 0;
}

bool Datapath::ToggleInterface(std::string_view ifname, bool up) {
  std::string_view link = up ? "up" : "down";
  return process_runner_->ip("link", "set", {ifname, link}) == 0;
}

bool Datapath::ConfigureInterface(std::string_view ifname,
                                  std::optional<net_base::MacAddress> mac_addr,
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
      std::string(ifname),
      up ? "up" : "down",
  };
  if (mac_addr) {
    iplink_args.insert(iplink_args.end(), {"addr", mac_addr->ToString()});
  }
  iplink_args.insert(iplink_args.end(),
                     {"multicast", enable_multicast ? "on" : "off"});
  return process_runner_->ip("link", "set", iplink_args) == 0;
}

void Datapath::RemoveInterface(std::string_view ifname) {
  process_runner_->ip("link", "delete", {ifname},
                      /*as_patchpanel_user=*/false,
                      /*log_failures=*/false);
}

bool Datapath::AddSourceIPv4DropRule(std::string_view oif,
                                     std::string_view src_ip) {
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
  StopRoutingDevice(nsinfo.host_ifname, nsinfo.source);
  RemoveInterface(nsinfo.host_ifname);
  NetnsDeleteName(nsinfo.netns_name);
}

bool Datapath::ModifyDnsProxyDNAT(IpFamily family,
                                  const DnsRedirectionRule& rule,
                                  Iptables::Command op,
                                  std::string_view ifname,
                                  std::string_view chain) {
  bool success = true;
  for (const auto& protocol : {"udp", "tcp"}) {
    std::vector<std::string_view> args;
    if (!ifname.empty()) {
      args.insert(args.end(), {"-i", ifname});
    }
    std::string proxy_addr = rule.proxy_address.ToString();
    args.insert(args.end(), {"-p", protocol, "--dport", kDefaultDnsPort, "-j",
                             "DNAT", "--to-destination", proxy_addr, "-w"});
    if (!ModifyIptables(family, Iptables::Table::kNat, op, chain, args)) {
      success = false;
    }
  }
  return success;
}

bool Datapath::ModifyDnsProxyMasquerade(IpFamily family,
                                        Iptables::Command op,
                                        std::string_view chain) {
  bool success = true;
  for (const auto& protocol : {"udp", "tcp"}) {
    std::vector<std::string_view> args = {
        "-p", protocol, "--dport", kDefaultDnsPort, "-j", "MASQUERADE", "-w"};
    if (!ModifyIptables(family, Iptables::Table::kNat, op, chain, args)) {
      success = false;
    }
  }
  return success;
}

bool Datapath::StartDnsRedirection(const DnsRedirectionRule& rule) {
  auto batch_mode = process_runner_->AcquireIptablesBatchMode();

  const IpFamily family = ConvertIpFamily(rule.proxy_address.GetFamily());
  switch (rule.type) {
    case patchpanel::SetDnsRedirectionRuleRequest::DEFAULT: {
      if (!ModifyDnsProxyDNAT(family, rule, Iptables::Command::kA,
                              rule.input_ifname, kRedirectDefaultDnsChain)) {
        LOG(ERROR) << "Failed to add DNS DNAT rule for " << rule.input_ifname;
        return false;
      }
      break;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::ARC: {
      break;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::USER: {
      // Start protecting DNS traffic from VPN fwmark tagging.
      if (!ModifyDnsRedirectionSkipVpnRule(family, Iptables::Command::kA)) {
        LOG(ERROR) << "Failed to add VPN skip rule for DNS proxy";
        return false;
      }

      // Add DNS redirect rule for user (including Chrome) traffic.
      if (!ModifyDnsProxyDNAT(family, rule, Iptables::Command::kA,
                              /*ifname=*/"", kRedirectUserDnsChain)) {
        LOG(ERROR) << "Failed to add user DNS DNAT rule";
        return false;
      }

      // Add MASQUERADE rule for user (including Chrome) traffic.
      if (family == IpFamily::kIPv6 &&
          !ModifyDnsProxyMasquerade(family, Iptables::Command::kA,
                                    kSNATUserDnsChain)) {
        LOG(ERROR) << "Failed to add user DNS MASQUERADE rule";
        return false;
      }

      // Allows user (including Chrome) traffic to go to user DNS proxy's
      // address.
      if (!ModifyDnsProxyAcceptRule(family, rule, Iptables::Command::kA)) {
        LOG(ERROR) << "Failed to add DNS proxy accept rule for "
                   << rule.host_ifname;
        return false;
      }
      break;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION: {
      if (!ModifyDnsExcludeDestinationRule(family, rule, Iptables::Command::kI,
                                           kRedirectUserDnsChain)) {
        LOG(ERROR) << "Failed to add user DNS exclude rule";
        return false;
      }
      if (!ModifyDnsProxyAcceptRule(family, rule, Iptables::Command::kA)) {
        LOG(ERROR) << "Failed to add DNS proxy accept rule for "
                   << rule.host_ifname;
        return false;
      }
      break;
    }
    default:
      LOG(ERROR) << "Invalid DNS proxy type " << rule;
      return false;
  }

  if (batch_mode) {
    return process_runner_->CommitIptablesRules(std::move(batch_mode));
  } else {
    // This means that the caller of this function already acquired the batch
    // mode. The execution will be done there.
    return true;
  }
}

void Datapath::StopDnsRedirection(const DnsRedirectionRule& rule) {
  auto batch_mode = process_runner_->AcquireIptablesBatchMode();

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
      ModifyDnsProxyDNAT(family, rule, Iptables::Command::kD, /*ifname=*/"",
                         kRedirectUserDnsChain);
      ModifyDnsRedirectionSkipVpnRule(family, Iptables::Command::kD);
      if (family == IpFamily::kIPv6) {
        ModifyDnsProxyMasquerade(family, Iptables::Command::kD,
                                 kSNATUserDnsChain);
      }
      ModifyDnsProxyAcceptRule(family, rule, Iptables::Command::kD);
      break;
    }
    case patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION: {
      ModifyDnsExcludeDestinationRule(family, rule, Iptables::Command::kD,
                                      kRedirectUserDnsChain);
      ModifyDnsProxyAcceptRule(family, rule, Iptables::Command::kD);
      break;
    }
    default:
      LOG(ERROR) << "Invalid DNS proxy type " << rule;
  }
}

void Datapath::AddDownstreamInterfaceRules(
    std::optional<ShillClient::Device> upstream_device,
    std::string_view int_ifname,
    TrafficSource source,
    bool static_ipv6) {
  auto forward_firewall_rule_type = GetForwardFirewallRuleType(source);
  if (forward_firewall_rule_type == ForwardFirewallRuleType::kTethering) {
    // Explicitly accept any traffic forwarded between the upstream and
    // downstream network interfaces.
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kA, kForwardTetheringChain, "ACCEPT",
                   upstream_device->ifname, int_ifname);
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kA, kForwardTetheringChain, "ACCEPT",
                   int_ifname, upstream_device->ifname);
    // Then, reject any other forwarded traffic between the downstream network
    // interface and any other interface.
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kA, kForwardTetheringChain, "DROP",
                   /*iif=*/"", int_ifname);
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kA, kForwardTetheringChain, "DROP",
                   int_ifname, /*oif=*/"");
    // If the upstream network is shared with the device (e.g. non-cellular
    // upstream, DEFAULT PDN used as upstream, DUN PDN upstream also used as
    // DEFAULT), it is not possible to drop any other forwarded traffic in or
    // out of the upstream network interface.
    // TODO(b/273749806): Make patchpanel aware of whether the upstream network
    // is exclusively used for tethering or not, and add the additional DROP
    // rules if that is the case.
  }

  if (forward_firewall_rule_type == ForwardFirewallRuleType::kLocalOnly) {
    // Reject any forwarded traffic between the downstream network interface
    // and any other interface.
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kA, kForwardLocalOnlyChain, "DROP",
                   /*iif=*/"", int_ifname);
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kA, kForwardLocalOnlyChain, "DROP",
                   int_ifname, /*oif=*/"");
  }

  if (forward_firewall_rule_type == ForwardFirewallRuleType::kOpen) {
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kA, "FORWARD", "ACCEPT", /*iif=*/"",
                   int_ifname);
  }
  if (!IsDownstreamNetworkForwardFirewallRule(forward_firewall_rule_type)) {
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kA, "FORWARD", "ACCEPT", int_ifname,
                   /*oif=*/"");
  }
  if (forward_firewall_rule_type == ForwardFirewallRuleType::kIsolatedGuest) {
    ModifyIsolatedGuestDropRule(Iptables::Command::kA, int_ifname);
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
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kMangle,
                 Iptables::Command::kA, "PREROUTING", subchain, int_ifname,
                 /*oif=*/"");
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
                                  std::string_view int_ifname,
                                  TrafficSource source,
                                  bool static_ipv6) {
  auto batch_mode = process_runner_->AcquireIptablesBatchMode();

  std::string_view ext_ifname = shill_device.ifname;
  AddDownstreamInterfaceRules(shill_device, int_ifname, source, static_ipv6);
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

  // Restores the source bits from the conntrack mark to the fwmark of a
  // packet. The source information specific to a particular connection in
  // conntrack table is always preferred over the default source value.
  // Such source tag overrides can be injected in conntrack with
  // ConntrackMonitor.
  if (!ModifyConnmarkRestore(IpFamily::kDual, subchain, Iptables::Command::kA,
                             /*iif=*/"", kFwmarkAllSourcesMask)) {
    LOG(ERROR) << "Failed to add CONNMARK restore rule in " << subchain;
  }

  // If the source bit from the connmark has already been restored to a known
  // traffic source mark, return. Otherwise mark with traffic source specified
  // in the args.
  std::string mark = SourceFwmarkWithMask(TrafficSource::kUnknown);
  ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                 Iptables::Command::kA, subchain,
                 {"-m", "mark", "!", "--mark", mark, "-j", "RETURN", "-w"});

  if (!ModifyFwmarkSourceTag(subchain, Iptables::Command::kA, source)) {
    LOG(ERROR) << "Failed to add source fwmark tagging rule for source "
               << source << " in " << subchain;
  }
}

void Datapath::StartRoutingDeviceAsSystem(std::string_view int_ifname,
                                          TrafficSource source,
                                          bool static_ipv6) {
  auto batch_mode = process_runner_->AcquireIptablesBatchMode();

  AddDownstreamInterfaceRules(std::nullopt, int_ifname, source, static_ipv6);

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
    std::string_view int_ifname,
    TrafficSource source,
    const IPv4Address& int_ipv4_addr,
    std::optional<net_base::IPv4Address> peer_ipv4_addr,
    std::optional<IPv6Address> int_ipv6_addr,
    std::optional<net_base::IPv6Address> peer_ipv6_addr) {
  auto batch_mode = process_runner_->AcquireIptablesBatchMode();

  AddDownstreamInterfaceRules(std::nullopt, int_ifname, source,
                              peer_ipv6_addr.has_value());

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
  if (!peer_ipv4_addr) {
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kMangle,
                   Iptables::Command::kA, subchain, kSkipApplyVpnMarkChain,
                   /*iif=*/"", /*oif=*/"");
  }

  // Forwarded traffic from downstream interfaces routed to the logical
  // default network is eligible to be routed through a VPN.
  if (!ModifyFwmarkVpnJumpRule(subchain, Iptables::Command::kA, {}, {}))
    LOG(ERROR) << "Failed to add jump rule to VPN chain for " << int_ifname;
}

void Datapath::StopRoutingDevice(std::string_view int_ifname,
                                 TrafficSource source) {
  auto batch_mode = process_runner_->AcquireIptablesBatchMode();

  auto forward_firewall_rule_type = GetForwardFirewallRuleType(source);
  if (forward_firewall_rule_type == ForwardFirewallRuleType::kTethering) {
    // Assume there is a single and unique tethering setup across the device.
    // Therefore, the tethering accept and drop rules can be removed by flushing
    // the relevant chain.
    FlushChain(IpFamily::kDual, Iptables::Table::kFilter,
               kForwardTetheringChain);
  }
  if (forward_firewall_rule_type == ForwardFirewallRuleType::kLocalOnly) {
    // Assume there is a single and unique local only downstream network setup
    // across the device. Therefore, the tethering accept and drop rules can be
    // removed by flushing the relevant chain.
    FlushChain(IpFamily::kDual, Iptables::Table::kFilter,
               kForwardLocalOnlyChain);
  }
  if (forward_firewall_rule_type == ForwardFirewallRuleType::kOpen) {
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kD, "FORWARD", "ACCEPT", /*iif=*/"",
                   int_ifname);
  }
  if (!IsDownstreamNetworkForwardFirewallRule(forward_firewall_rule_type)) {
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kD, "FORWARD", "ACCEPT", int_ifname,
                   /*oif=*/"");
  }
  if (forward_firewall_rule_type == ForwardFirewallRuleType::kIsolatedGuest) {
    ModifyIsolatedGuestDropRule(Iptables::Command::kD, int_ifname);
  }
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
                                  std::string_view dns_ipv4_addr) {
  std::string_view ifname = shill_device.ifname;
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
  std::string_view ifname = shill_device.ifname;
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
                                         std::string_view protocol,
                                         std::string_view ifname,
                                         std::string_view dns_ipv4_addr) {
  std::vector<std::string_view> args = {
      "-p", protocol, "--dport",          "53",          "-o", ifname,
      "-j", "DNAT",   "--to-destination", dns_ipv4_addr, "-w"};
  return ModifyIptables(IpFamily::kIPv4, Iptables::Table::kNat, op,
                        kRedirectDnsChain, args);
}

bool Datapath::ModifyRedirectDnsJumpRule(IpFamily family,
                                         Iptables::Command op,
                                         std::string_view chain,
                                         std::string_view ifname,
                                         std::string_view target_chain,
                                         Fwmark mark,
                                         Fwmark mask,
                                         bool redirect_on_mark) {
  std::vector<std::string_view> args;
  if (!ifname.empty()) {
    args.insert(args.end(), {"-i", ifname});
  }
  std::string mark_str = base::StrCat({mark.ToString(), "/", mask.ToString()});
  if (mark.Value() != 0 && mask.Value() != 0) {
    args.insert(args.end(), {"-m", "mark"});
    if (!redirect_on_mark) {
      args.push_back("!");
    }
    args.insert(args.end(), {"--mark", mark_str});
  }
  args.insert(args.end(), {"-j", target_chain, "-w"});
  return ModifyIptables(family, Iptables::Table::kNat, op, chain, args);
}

bool Datapath::ModifyDnsProxyAcceptRule(IpFamily family,
                                        const DnsRedirectionRule& rule,
                                        Iptables::Command op) {
  std::string proxy_addr = rule.proxy_address.ToString();
  std::vector<std::string_view> args = {"-d", proxy_addr, "-j", "ACCEPT", "-w"};
  return ModifyIptables(family, Iptables::Table::kFilter, op,
                        kAcceptEgressToDnsProxyChain, args);
}

bool Datapath::ModifyDnsRedirectionSkipVpnRule(IpFamily family,
                                               Iptables::Command op) {
  bool success = true;
  for (const auto& protocol : {"udp", "tcp"}) {
    std::vector<std::string_view> args = {
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
                                               std::string_view chain) {
  bool success = true;
  std::string proxy_addr = rule.proxy_address.ToString();
  for (const auto& protocol : {"udp", "tcp"}) {
    std::vector<std::string_view> args = {
        "-p",      protocol,        "!",  "-d",     proxy_addr,
        "--dport", kDefaultDnsPort, "-j", "RETURN", "-w",
    };
    if (!ModifyIptables(family, Iptables::Table::kNat, op, chain, args)) {
      success = false;
    }
  }
  return success;
}

bool Datapath::MaskInterfaceFlags(std::string_view ifname,
                                  uint16_t on,
                                  uint16_t off) {
  base::ScopedFD sock(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!sock.is_valid()) {
    PLOG(ERROR) << "Failed to create control socket";
    return false;
  }
  struct ifreq ifr;
  FillInterfaceRequest(ifname, &ifr);
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
    std::string_view ifname,
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

bool Datapath::AddIPv6NeighborProxy(std::string_view ifname,
                                    const net_base::IPv6Address& ipv6_addr) {
  return process_runner_->ip6("neighbor", "add",
                              {"proxy", ipv6_addr.ToString(), "dev", ifname}) ==
         0;
}

void Datapath::RemoveIPv6NeighborProxy(std::string_view ifname,
                                       const net_base::IPv6Address& ipv6_addr) {
  process_runner_->ip6("neighbor", "del",
                       {"proxy", ipv6_addr.ToString(), "dev", ifname});
}

bool Datapath::AddIPv6Address(std::string_view ifname,
                              std::string_view ipv6_addr) {
  return process_runner_->ip6("addr", "add", {ipv6_addr, "dev", ifname}) == 0;
}

void Datapath::RemoveIPv6Address(std::string_view ifname,
                                 std::string_view ipv6_addr) {
  process_runner_->ip6("addr", "del", {ipv6_addr, "dev", ifname});
}

void Datapath::StartConnectionPinning(const ShillClient::Device& shill_device) {
  auto batch_mode = process_runner_->AcquireIptablesBatchMode();

  std::string_view ext_ifname = shill_device.ifname;
  int ifindex = system_->IfNametoindex(ext_ifname);
  if (ifindex == 0) {
    // Can happen if the interface has already been removed (b/183679000).
    LOG(ERROR) << "Failed to set up connection pinning on " << ext_ifname;
    return;
  }

  std::string subchain = base::StrCat({"POSTROUTING_", ext_ifname});
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
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kMangle,
                 Iptables::Command::kA, "POSTROUTING", subchain,
                 /*iif=*/"", ext_ifname);

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
  std::string_view ext_ifname = shill_device.ifname;
  std::string subchain = base::StrCat({"POSTROUTING_", ext_ifname});
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
  std::string_view vpn_ifname = vpn_device.ifname;
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
  ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kNat, Iptables::Command::kA,
                 "POSTROUTING", "MASQUERADE",
                 /*iif=*/"", vpn_ifname);
  StartConnectionPinning(vpn_device);

  std::string mark = base::StrCat({"0x0/", kFwmarkRoutingMask.ToString()});
  // Any traffic that already has a routing tag applied is accepted.
  if (!ModifyIptables(
          IpFamily::kDual, Iptables::Table::kMangle, Iptables::Command::kA,
          kApplyVpnMarkChain,
          {"-m", "mark", "!", "--mark", mark, "-j", "ACCEPT", "-w"})) {
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
  mark = base::StrCat(
      {routing_mark.value().ToString(), "/", kFwmarkRoutingMask.ToString()});
  if (!ModifyIptables(IpFamily::kDual, Iptables::Table::kFilter,
                      Iptables::Command::kA, kVpnAcceptChain,
                      {"-m", "mark", "--mark", mark, "-j", "ACCEPT", "-w"})) {
    LOG(ERROR) << "Failed to set filter rule for accepting VPN marked traffic";
  }
}

void Datapath::StopVpnRouting(const ShillClient::Device& vpn_device) {
  std::string_view vpn_ifname = vpn_device.ifname;
  LOG(INFO) << "Stop VPN routing on " << vpn_ifname;
  if (!FlushChain(IpFamily::kDual, Iptables::Table::kFilter, kVpnAcceptChain)) {
    LOG(ERROR) << "Could not flush " << kVpnAcceptChain;
  }
  if (vpn_ifname != kArcbr0Ifname) {
    StopRoutingDevice(kArcbr0Ifname, kArc);
  }
  if (!FlushChain(IpFamily::kDual, Iptables::Table::kMangle,
                  kApplyVpnMarkChain)) {
    LOG(ERROR) << "Could not flush " << kApplyVpnMarkChain;
  }
  StopConnectionPinning(vpn_device);
  ModifyJumpRule(IpFamily::kIPv4, Iptables::Table::kNat, Iptables::Command::kD,
                 "POSTROUTING", "MASQUERADE",
                 /*iif=*/"", vpn_ifname);
  if (!ModifyRedirectDnsJumpRule(
          IpFamily::kIPv4, Iptables::Command::kD, "OUTPUT",
          /*ifname=*/"", kRedirectDnsChain, kFwmarkRouteOnVpn, kFwmarkVpnMask,
          /*redirect_on_mark=*/false)) {
    LOG(ERROR) << "Failed to remove jump rule to " << kRedirectDnsChain;
  }
}

void Datapath::SetVpnLockdown(bool enable_vpn_lockdown) {
  if (enable_vpn_lockdown) {
    std::string mark = base::StrCat(
        {kFwmarkRouteOnVpn.ToString(), "/", kFwmarkVpnMask.ToString()});
    if (!ModifyIptables(IpFamily::kDual, Iptables::Table::kFilter,
                        Iptables::Command::kA, kVpnLockdownChain,
                        {"-m", "mark", "--mark", mark, "-j", "REJECT", "-w"})) {
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
    std::string prefix_str = prefix->ToString();
    if (!ModifyIptables(IpFamily::kIPv6, Iptables::Table::kFilter,
                        Iptables::Command::kA, subchain,
                        {"-s", prefix_str, "-j", "RETURN", "-w"})) {
      LOG(ERROR) << __func__ << ": Failed to add " << prefix_str
                 << " RETURN rule in " << subchain;
    }
  }
  ModifyJumpRule(IpFamily::kIPv6, Iptables::Table::kFilter,
                 Iptables::Command::kA, subchain, kEnforceSourcePrefixChain,
                 /*iif=*/"", /*oif=*/"");
}

bool Datapath::StartDownstreamNetwork(const DownstreamNetworkInfo& info) {
  if (info.topology == DownstreamNetworkTopology::kTethering &&
      !info.upstream_device) {
    LOG(ERROR) << __func__ << " " << info << ": no upstream Device defined";
    return false;
  } else if (info.topology == DownstreamNetworkTopology::kLocalOnly &&
             info.upstream_device) {
    LOG(ERROR) << __func__ << " " << info
               << ": invalid upstream Device argument";
    return false;
  }

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

  if (!ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                      Iptables::Command::kI, "OUTPUT",
                      GetEgressFilterChainName(info.topology),
                      /*iif=*/"", /*oif=*/info.downstream_ifname)) {
    return false;
  }
  if (!ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                      Iptables::Command::kI, kIngressDownstreamNetworkChain,
                      GetIngressFilterChainName(info.topology),
                      /*iif=*/info.downstream_ifname, /*oif=*/"")) {
    ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                   Iptables::Command::kD, "OUTPUT",
                   GetEgressFilterChainName(info.topology),
                   /*iif=*/"", /*oif=*/info.downstream_ifname);
    return false;
  }

  switch (info.topology) {
    case DownstreamNetworkTopology::kLocalOnly:
      // TODO(b:309710428): Replace StartRoutingDeviceAsSystem with local-only
      // routing mode to prevent forwarding to an external physical or virtual
      // network.
      StartRoutingDeviceAsSystem(info.downstream_ifname,
                                 info.GetTrafficSource(),
                                 /*static_ipv6=*/false);
      break;
    case DownstreamNetworkTopology::kTethering:
      // int_ipv4_addr is not necessary if route_on_vpn == false
      StartRoutingDevice(*info.upstream_device, info.downstream_ifname,
                         info.GetTrafficSource());
      break;
  }

  return true;
}

void Datapath::StopDownstreamNetwork(const DownstreamNetworkInfo& info) {
  if (info.topology == DownstreamNetworkTopology::kTethering &&
      !info.upstream_device) {
    LOG(ERROR) << __func__ << " " << info << ": no upstream Device defined";
    return;
  } else if (info.topology == DownstreamNetworkTopology::kLocalOnly &&
             info.upstream_device) {
    LOG(ERROR) << __func__ << " " << info
               << ": invalid upstream Device argument";
    return;
  }

  // Skip unconfiguring the downstream interface: shill will either destroy it
  // or flip it back to client mode and restart a Network on top.
  StopRoutingDevice(info.downstream_ifname, info.GetTrafficSource());
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                 Iptables::Command::kD, "OUTPUT",
                 GetEgressFilterChainName(info.topology),
                 /*iif=*/"", /*oif=*/info.downstream_ifname);
  ModifyJumpRule(IpFamily::kDual, Iptables::Table::kFilter,
                 Iptables::Command::kD, kIngressDownstreamNetworkChain,
                 GetIngressFilterChainName(info.topology),
                 /*iif=*/info.downstream_ifname, /*oif=*/"");
}

bool Datapath::ModifyConnmarkSet(IpFamily family,
                                 std::string_view chain,
                                 Iptables::Command op,
                                 Fwmark mark,
                                 Fwmark mask) {
  std::string mark_str = base::StrCat({mark.ToString(), "/", mask.ToString()});
  return ModifyIptables(family, Iptables::Table::kMangle, op, chain,
                        {"-j", "CONNMARK", "--set-mark", mark_str, "-w"});
}

bool Datapath::ModifyConnmarkRestore(IpFamily family,
                                     std::string_view chain,
                                     Iptables::Command op,
                                     std::string_view iif,
                                     Fwmark mask,
                                     bool skip_on_non_empty_mark) {
  std::vector<std::string_view> args;
  if (!iif.empty()) {
    args.insert(args.end(), {"-i", iif});
  }
  std::string mark_str;
  std::string mask_str = mask.ToString();
  if (skip_on_non_empty_mark) {
    mark_str = base::StrCat({"0x0/", mask_str});
    args.insert(args.end(), {"-m", "mark", "--mark", mark_str});
  }
  args.insert(args.end(),
              {"-j", "CONNMARK", "--restore-mark", "--mask", mask_str, "-w"});
  return ModifyIptables(family, Iptables::Table::kMangle, op, chain, args);
}

bool Datapath::ModifyConnmarkSave(IpFamily family,
                                  std::string_view chain,
                                  Iptables::Command op,
                                  Fwmark mask) {
  std::string mask_str = mask.ToString();
  std::vector<std::string_view> args = {"-j",     "CONNMARK", "--save-mark",
                                        "--mask", mask_str,   "-w"};
  return ModifyIptables(family, Iptables::Table::kMangle, op, chain, args);
}

bool Datapath::ModifyFwmarkRoutingTag(std::string_view chain,
                                      Iptables::Command op,
                                      Fwmark routing_mark) {
  return ModifyFwmark(IpFamily::kDual, chain, op, /*int_ifname=*/"",
                      /*uid_name=*/"", /*classid=*/0, routing_mark,
                      kFwmarkRoutingMask);
}

bool Datapath::ModifyFwmarkSourceTag(std::string_view chain,
                                     Iptables::Command op,
                                     TrafficSource source) {
  return ModifyFwmark(IpFamily::kDual, chain, op, /*iif=*/"", /*uid_name=*/"",
                      /*classid=*/0, Fwmark::FromSource(source),
                      kFwmarkAllSourcesMask);
}

bool Datapath::ModifyFwmark(IpFamily family,
                            std::string_view chain,
                            Iptables::Command op,
                            std::string_view iif,
                            std::string_view uid_name,
                            uint32_t classid,
                            Fwmark mark,
                            Fwmark mask,
                            bool log_failures) {
  std::vector<std::string_view> args;
  if (!iif.empty()) {
    args.insert(args.end(), {"-i", iif});
  }
  if (!uid_name.empty()) {
    args.insert(args.end(), {"-m", "owner", "--uid-owner", uid_name});
  }
  std::string id = base::StringPrintf("0x%08x", classid);
  if (classid != 0) {
    args.insert(args.end(), {"-m", "cgroup", "--cgroup", id});
  }
  std::string mask_str = base::StrCat({mark.ToString(), "/", mask.ToString()});
  args.insert(args.end(), {"-j", "MARK", "--set-mark", mask_str, "-w"});

  return ModifyIptables(family, Iptables::Table::kMangle, op, chain, args,
                        log_failures);
}

bool Datapath::ModifyJumpRule(IpFamily family,
                              Iptables::Table table,
                              Iptables::Command op,
                              std::string_view chain,
                              std::string_view target,
                              std::string_view iif,
                              std::string_view oif,
                              bool log_failures) {
  std::vector<std::string_view> args;
  if (!iif.empty()) {
    args.insert(args.end(), {"-i", iif});
  }
  if (!oif.empty()) {
    args.insert(args.end(), {"-o", oif});
  }
  args.insert(args.end(), {"-j", target, "-w"});
  if (!ModifyIptables(family, table, op, chain, args, log_failures)) {
    if (log_failures) {
      LOG(ERROR) << __func__ << " failure: " << family << " -t " << table << " "
                 << op << " " << chain
                 << (iif.empty() ? "" : base::StrCat({" -i ", iif}))
                 << (oif.empty() ? "" : base::StrCat({" -o ", oif})) << " -j "
                 << target;
    }
    return false;
  }
  return true;
}

bool Datapath::ModifyFwmarkVpnJumpRule(std::string_view chain,
                                       Iptables::Command op,
                                       Fwmark mark,
                                       Fwmark mask) {
  std::vector<std::string_view> args;
  std::string mark_str = base::StrCat({mark.ToString(), "/", mask.ToString()});
  if (mark.Value() != 0 && mask.Value() != 0) {
    args.insert(args.end(), {"-m", "mark", "--mark", mark_str});
  }
  args.insert(args.end(), {"-j", kApplyVpnMarkChain, "-w"});
  return ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, op, chain,
                        args);
}

bool Datapath::CheckChain(IpFamily family,
                          Iptables::Table table,
                          std::string_view name) {
  return ModifyChain(family, table, Iptables::Command::kC, name,
                     /*log_failures=*/false);
}

bool Datapath::AddChain(IpFamily family,
                        Iptables::Table table,
                        std::string_view chain) {
  DCHECK(chain.size() <= kIptablesMaxChainLength)
      << "chain name " << chain << " is longer than "
      << kIptablesMaxChainLength;
  return ModifyChain(family, table, Iptables::Command::kN, chain);
}

bool Datapath::RemoveChain(IpFamily family,
                           Iptables::Table table,
                           std::string_view chain) {
  return ModifyChain(family, table, Iptables::Command::kX, chain);
}

bool Datapath::FlushChain(IpFamily family,
                          Iptables::Table table,
                          std::string_view chain) {
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
                              const std::vector<std::string_view>& argv,
                              bool log_failures) {
  bool success = true;
  if (family == IpFamily::kIPv4 || family == IpFamily::kDual) {
    // TODO(b/325359902): Change |argv| to span type and delete conversion.
    success &= process_runner_->iptables(
                   table, command, chain,
                   const_cast<std::vector<std::string_view>&>(argv),
                   log_failures) == 0;
  }
  if (family == IpFamily::kIPv6 || family == IpFamily::kDual) {
    // TODO(b/325359902): Change |argv| to span type and delete conversion.
    success &= process_runner_->ip6tables(
                   table, command, chain,
                   const_cast<std::vector<std::string_view>&>(argv),
                   log_failures) == 0;
  }
  return success;
}

std::string Datapath::DumpIptables(IpFamily family, Iptables::Table table) {
  std::string result;
  std::vector<std::string_view> argv = {"-x", "-v", "-n", "-w"};
  switch (family) {
    case IpFamily::kIPv4:
      if (process_runner_->iptables(table, Iptables::Command::kL, /*chain=*/"",
                                    argv,
                                    /*log_failures=*/true, &result) != 0) {
        LOG(ERROR) << "Could not dump iptables " << table;
      }
      break;
    case IpFamily::kIPv6:
      if (process_runner_->ip6tables(table, Iptables::Command::kL, /*chain=*/"",
                                     argv,
                                     /*log_failures=*/true, &result) != 0) {
        LOG(ERROR) << "Could not dump ip6tables " << table;
      }
      break;
    case IpFamily::kDual:
      LOG(ERROR) << "Cannot dump iptables and ip6tables at the same time";
      break;
  }
  return result;
}

bool Datapath::AddIPv4RouteToTable(std::string_view ifname,
                                   const net_base::IPv4CIDR& ipv4_cidr,
                                   int table_id) {
  return process_runner_->ip("route", "add",
                             {ipv4_cidr.ToString(), "dev", ifname, "table",
                              base::NumberToString(table_id)}) == 0;
}

void Datapath::DeleteIPv4RouteFromTable(std::string_view ifname,
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

bool Datapath::AddAdbPortForwardRule(std::string_view ifname) {
  return firewall_->AddIpv4ForwardRule(patchpanel::ModifyPortRuleRequest::TCP,
                                       kArcAddr, kAdbServerPort, ifname,
                                       kLocalhostAddr, kAdbProxyTcpListenPort);
}

void Datapath::DeleteAdbPortForwardRule(std::string_view ifname) {
  firewall_->DeleteIpv4ForwardRule(patchpanel::ModifyPortRuleRequest::TCP,
                                   kArcAddr, kAdbServerPort, ifname,
                                   kLocalhostAddr, kAdbProxyTcpListenPort);
}

bool Datapath::AddAdbPortAccessRule(std::string_view ifname) {
  return firewall_->AddAcceptRules(patchpanel::ModifyPortRuleRequest::TCP,
                                   kAdbProxyTcpListenPort, ifname);
}

void Datapath::DeleteAdbPortAccessRule(std::string_view ifname) {
  firewall_->DeleteAcceptRules(patchpanel::ModifyPortRuleRequest::TCP,
                               kAdbProxyTcpListenPort, ifname);
}

bool Datapath::SetConntrackHelpers(const bool enable_helpers) {
  return system_->SysNetSet(System::SysNet::kConntrackHelper,
                            enable_helpers ? "1" : "0");
}

bool Datapath::SetRouteLocalnet(std::string_view ifname, const bool enable) {
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
                 "POSTROUTING", {"-o", ifname, "-j", kQoSApplyDSCPChain, "-w"});
}

void Datapath::AddBorealisQoSRule(std::string_view ifname) {
  std::string mark = QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive);
  ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                 Iptables::Command::kA, kQoSDetectBorealisChain,
                 {"-i", ifname, "-j", "MARK", "--set-xmark", mark, "-w"});
}

void Datapath::RemoveBorealisQoSRule(std::string_view ifname) {
  std::string mark = QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive);
  ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                 Iptables::Command::kD, kQoSDetectBorealisChain,
                 {"-i", ifname, "-j", "MARK", "--set-xmark", mark, "-w"});
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
  std::string ip = base::JoinString(ip_strs, ",");
  std::string mark = QoSFwmarkWithMask(QoSCategory::kNetworkControl);
  for (std::string_view protocol : {"udp", "tcp"}) {
    ModifyIptables(family, Iptables::Table::kMangle, Iptables::Command::kA,
                   kQoSDetectDoHChain,
                   {"-p", protocol, "--dport", "443", "-d", ip, "-j", "MARK",
                    "--set-xmark", mark, "-w"});
  }
}

bool Datapath::ModifyIsolatedGuestDropRule(Iptables::Command command,
                                           std::string_view ifname) {
  bool success = true;
  success &= ModifyIptables(IpFamily::kDual, Iptables::Table::kFilter, command,
                            kDropForwardToBruschettaChain,
                            {"-o", ifname, "-j", "DROP", "-w"});
  success &= ModifyIptables(
      IpFamily::kDual, Iptables::Table::kFilter, command,
      kDropOutputToBruschettaChain,
      {"-m", "state", "--state", "NEW", "-o", ifname, "-j", "DROP", "-w"});
  return success;
}

bool Datapath::ModifyClatAcceptRules(Iptables::Command command,
                                     std::string_view ifname) {
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

std::ostream& operator<<(std::ostream& stream, IpFamily family) {
  switch (family) {
    case IpFamily::kIPv4:
      return stream << "IPv4";
    case IpFamily::kIPv6:
      return stream << "IPv6";
    case IpFamily::kDual:
      return stream << "IPv4v6";
  }
}

}  // namespace patchpanel
