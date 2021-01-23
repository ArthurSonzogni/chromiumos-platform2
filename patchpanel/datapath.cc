// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/datapath.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <algorithm>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/userdb_utils.h>

#include "patchpanel/adb_proxy.h"
#include "patchpanel/arc_service.h"
#include "patchpanel/net_util.h"
#include "patchpanel/scoped_ns.h"

namespace patchpanel {

namespace {
// TODO(hugobenichi) Consolidate this constant definition in a single place.
constexpr pid_t kTestPID = -2;
constexpr char kDefaultIfname[] = "vmtap%d";
constexpr char kTunDev[] = "/dev/net/tun";
constexpr char kArcAddr[] = "100.115.92.2";
constexpr char kLocalhostAddr[] = "127.0.0.1";
constexpr uint16_t kAdbServerPort = 5555;

// Constants used for dropping locally originated traffic bound to an incorrect
// source IPv4 address.
constexpr char kGuestIPv4Subnet[] = "100.115.92.0/23";
constexpr std::array<const char*, 6> kPhysicalIfnamePrefixes{
    {"eth+", "wlan+", "mlan+", "usb+", "wwan+", "rmnet+"}};

constexpr char kApplyLocalSourceMarkChain[] = "apply_local_source_mark";
constexpr char kApplyVpnMarkChain[] = "apply_vpn_mark";
constexpr char kCheckRoutingMarkChain[] = "check_routing_mark";

// Constant fwmark mask for matching local socket traffic that should be routed
// through a VPN connection. The traffic must not be part of an existing
// connection and must match exactly the VPN routing intent policy bit.
const Fwmark kFwmarkVpnMatchingMask = kFwmarkRoutingMask | kFwmarkVpnMask;

std::string PrefixIfname(const std::string& prefix, const std::string& ifname) {
  std::string n = prefix + ifname;
  if (n.length() < IFNAMSIZ)
    return n;

  // Best effort attempt to preserve the interface number, assuming it's the
  // last char in the name.
  auto c = ifname[ifname.length() - 1];
  n.resize(IFNAMSIZ - 1);
  n[n.length() - 1] = c;
  return n;
}

}  // namespace

std::string ArcVethHostName(const std::string& ifname) {
  return PrefixIfname("veth", ifname);
}

std::string ArcBridgeName(const std::string& ifname) {
  return PrefixIfname("arc_", ifname);
}

Datapath::Datapath(MinijailedProcessRunner* process_runner, Firewall* firewall)
    : Datapath(process_runner, firewall, ioctl) {}

Datapath::Datapath(MinijailedProcessRunner* process_runner,
                   Firewall* firewall,
                   ioctl_t ioctl_hook)
    : process_runner_(process_runner), firewall_(firewall), ioctl_(ioctl_hook) {
  CHECK(process_runner_);
}

MinijailedProcessRunner& Datapath::runner() const {
  return *process_runner_;
}

void Datapath::Start() {
  // Enable IPv4 packet forwarding
  if (process_runner_->sysctl_w("net.ipv4.ip_forward", "1") != 0)
    LOG(ERROR) << "Failed to update net.ipv4.ip_forward."
               << " Guest connectivity will not work correctly.";

  // Limit local port range: Android owns 47104-61000.
  // TODO(garrick): The original history behind this tweak is gone. Some
  // investigation is needed to see if it is still applicable.
  if (process_runner_->sysctl_w("net.ipv4.ip_local_port_range",
                                "32768 47103") != 0)
    LOG(ERROR) << "Failed to limit local port range. Some Android features or"
               << " apps may not work correctly.";

  // Enable IPv6 packet forwarding
  if (process_runner_->sysctl_w("net.ipv6.conf.all.forwarding", "1") != 0)
    LOG(ERROR) << "Failed to update net.ipv6.conf.all.forwarding."
               << " IPv6 functionality may be broken.";

  if (!AddSNATMarkRules())
    LOG(ERROR) << "Failed to install SNAT mark rules."
               << " Guest connectivity may be broken.";

  // Create a FORWARD ACCEPT rule for connections already established.
  if (process_runner_->iptables(
          "filter", {"-A", "FORWARD", "-m", "state", "--state",
                     "ESTABLISHED,RELATED", "-j", "ACCEPT", "-w"}) != 0)
    LOG(ERROR) << "Failed to install forwarding rule for established"
               << " connections.";

  // chromium:898210: Drop any locally originated traffic that would exit a
  // physical interface with a source IPv4 address from the subnet of IPs used
  // for VMs, containers, and connected namespaces This is needed to prevent
  // packets leaking with an incorrect src IP when a local process binds to the
  // wrong interface.
  for (const auto& oif : kPhysicalIfnamePrefixes) {
    if (!AddSourceIPv4DropRule(oif, kGuestIPv4Subnet))
      LOG(WARNING) << "Failed to set up IPv4 drop rule for src ip "
                   << kGuestIPv4Subnet << " exiting " << oif;
  }

  if (!AddOutboundIPv4SNATMark("vmtap+"))
    LOG(ERROR) << "Failed to set up NAT for TAP devices."
               << " Guest connectivity may be broken.";

  // b/176260499: on 4.4 kernel, the following connmark rules are observed to
  // wrongly cause neighbor discovery icmpv6 packets to be dropped. Add these
  // rules to bypass connmark rule for those packets.
  for (const auto& type : kNeighborDiscoveryTypes) {
    if (!ModifyIptables(IpFamily::IPv6, "mangle",
                        {"-A", "OUTPUT", "-p", "icmpv6", "--icmpv6-type", type,
                         "-j", "ACCEPT", "-w"}))
      LOG(ERROR) << "Failed to set up connmark bypass rule for " << type
                 << " packets";
  }

  // Applies the routing tag saved in conntrack for any established connection
  // for sockets created in the host network namespace.
  if (!ModifyConnmarkRestore(IpFamily::Dual, "OUTPUT", "-A", "" /*iif*/,
                             kFwmarkRoutingMask))
    LOG(ERROR) << "Failed to add OUTPUT CONNMARK restore rule";
  // b/177787823 Also restore the routing tag after routing has taken place so
  // that packets pre-tagged with the VPN routing tag are in sync with their
  // associated CONNMARK routing tag. This is necessary to correctly identify
  // packets exiting through the wrong interface.
  if (!ModifyConnmarkRestore(IpFamily::Dual, "POSTROUTING", "-A", "" /*iif*/,
                             kFwmarkRoutingMask))
    LOG(ERROR) << "Failed to add POSTROUTING CONNMARK restore rule";

  // Set up a mangle chain used in OUTPUT for applying the fwmark TrafficSource
  // tag and tagging the local traffic that should be routed through a VPN.
  if (!ModifyChain(IpFamily::Dual, "mangle", "-N", kApplyLocalSourceMarkChain))
    LOG(ERROR) << "Failed to set up " << kApplyLocalSourceMarkChain
               << " mangle chain";
  // Ensure that the chain is empty if patchpanel is restarting after a crash.
  if (!ModifyChain(IpFamily::Dual, "mangle", "-F", kApplyLocalSourceMarkChain))
    LOG(ERROR) << "Failed to flush " << kApplyLocalSourceMarkChain
               << " mangle chain";
  if (!ModifyIptables(IpFamily::Dual, "mangle",
                      {"-A", "OUTPUT", "-j", kApplyLocalSourceMarkChain, "-w"}))
    LOG(ERROR) << "Failed to attach " << kApplyLocalSourceMarkChain
               << " to mangle OUTPUT";
  // Create rules for tagging local sources with the source tag and the vpn
  // policy tag.
  for (const auto& source : kLocalSourceTypes) {
    if (!ModifyFwmarkLocalSourceTag("-A", source))
      LOG(ERROR) << "Failed to create fwmark tagging rule for uid " << source
                 << " in " << kApplyLocalSourceMarkChain;
  }
  // Finally add a catch-all rule for tagging any remaining local sources with
  // the SYSTEM source tag
  if (!ModifyFwmarkDefaultLocalSourceTag("-A", TrafficSource::SYSTEM))
    LOG(ERROR) << "Failed to set up rule tagging traffic with default source";

  // Sets up a mangle chain used in OUTPUT and PREROUTING for tagging "user"
  // traffic that should be routed through a VPN.
  if (!ModifyChain(IpFamily::Dual, "mangle", "-N", kApplyVpnMarkChain))
    LOG(ERROR) << "Failed to set up " << kApplyVpnMarkChain << " mangle chain";
  // Ensure that the chain is empty if patchpanel is restarting after a crash.
  if (!ModifyChain(IpFamily::Dual, "mangle", "-F", kApplyVpnMarkChain))
    LOG(ERROR) << "Failed to flush " << kApplyVpnMarkChain << " mangle chain";
  // All local outgoing traffic eligible to VPN routing should traverse the VPN
  // marking chain.
  if (!ModifyFwmarkVpnJumpRule("OUTPUT", "-A", "" /*iif*/, kFwmarkRouteOnVpn,
                               kFwmarkVpnMask))
    LOG(ERROR) << "Failed to add jump rule to VPN chain in mangle OUTPUT chain";
  // Any traffic that already has a routing tag applied is accepted.
  if (!ModifyIptables(
          IpFamily::Dual, "mangle",
          {"-A", kApplyVpnMarkChain, "-m", "mark", "!", "--mark",
           "0x0/" + kFwmarkRoutingMask.ToString(), "-j", "ACCEPT", "-w"}))
    LOG(ERROR) << "Failed to add ACCEPT rule to VPN tagging chain for marked "
                  "connections";

  // Sets up a mangle chain used in POSTROUTING for checking consistency between
  // the routing tag and the output interface.
  if (!ModifyChain(IpFamily::Dual, "mangle", "-N", kCheckRoutingMarkChain))
    LOG(ERROR) << "Failed to set up " << kCheckRoutingMarkChain
               << " mangle chain";
  // Ensure that the chain is empty if patchpanel is restarting after a crash.
  if (!ModifyChain(IpFamily::Dual, "mangle", "-F", kCheckRoutingMarkChain))
    LOG(ERROR) << "Failed to flush " << kCheckRoutingMarkChain
               << " mangle chain";

  // b/177787823 If it already exists, the routing tag of any traffic exiting an
  // interface (physical or VPN) must match the routing tag of that interface.
  if (!ModifyIptables(IpFamily::Dual, "mangle",
                      {"-A", "POSTROUTING", "-m", "mark", "!", "--mark",
                       "0x0/" + kFwmarkRoutingMask.ToString(), "-j",
                       kCheckRoutingMarkChain, "-w"}))
    LOG(ERROR) << "Failed to add POSTROUTING jump rule to "
               << kCheckRoutingMarkChain;
}

void Datapath::Stop() {
  RemoveOutboundIPv4SNATMark("vmtap+");
  process_runner_->iptables("filter",
                            {"-D", "FORWARD", "-m", "state", "--state",
                             "ESTABLISHED,RELATED", "-j", "ACCEPT", "-w"});
  RemoveSNATMarkRules();
  for (const auto& oif : kPhysicalIfnamePrefixes)
    RemoveSourceIPv4DropRule(oif, kGuestIPv4Subnet);

  // Restore original local port range.
  // TODO(garrick): The original history behind this tweak is gone. Some
  // investigation is needed to see if it is still applicable.
  if (process_runner_->sysctl_w("net.ipv4.ip_local_port_range",
                                "32768 61000") != 0)
    LOG(ERROR) << "Failed to restore local port range";

  // Disable packet forwarding
  if (process_runner_->sysctl_w("net.ipv6.conf.all.forwarding", "0") != 0)
    LOG(ERROR) << "Failed to restore net.ipv6.conf.all.forwarding.";

  if (process_runner_->sysctl_w("net.ipv4.ip_forward", "0") != 0)
    LOG(ERROR) << "Failed to restore net.ipv4.ip_forward.";

  // Detach the VPN marking mangle chain
  if (!ModifyFwmarkVpnJumpRule("OUTPUT", "-D", "" /*iif*/, kFwmarkRouteOnVpn,
                               kFwmarkVpnMask))
    LOG(ERROR)
        << "Failed to remove from mangle OUTPUT chain jump rule to VPN chain";

  // Detach apply_local_source_mark from mangle PREROUTING
  if (!ModifyIptables(IpFamily::Dual, "mangle",
                      {"-D", "OUTPUT", "-j", kApplyLocalSourceMarkChain, "-w"}))
    LOG(ERROR) << "Failed to detach " << kApplyLocalSourceMarkChain
               << " from mangle OUTPUT";

  // Stops applying routing tags saved in conntrack for sockets created in the
  // host network namespace.
  if (!ModifyConnmarkRestore(IpFamily::Dual, "OUTPUT", "-D", "" /*iif*/,
                             kFwmarkRoutingMask))
    LOG(ERROR) << "Failed to remove OUTPUT CONNMARK restore rule";
  if (!ModifyConnmarkRestore(IpFamily::Dual, "POSTROUTING", "-D", "",
                             kFwmarkRoutingMask))
    LOG(ERROR) << "Failed to remove POSTROUTING CONNMARK restore rule";

  // Delete the POSTROUTING jump rule to check_routing_mark chain holding
  // routing tag filter rules.
  if (!ModifyIptables(IpFamily::Dual, "mangle",
                      {"-D", "POSTROUTING", "-m", "mark", "!", "--mark",
                       "0x0/" + kFwmarkRoutingMask.ToString(), "-j",
                       kCheckRoutingMarkChain, "-w"}))
    LOG(ERROR) << "Failed to remove POSTROUTING jump rule to "
               << kCheckRoutingMarkChain;

  // Delete the mangle chains
  for (const auto* chain : {kApplyLocalSourceMarkChain, kApplyVpnMarkChain,
                            kCheckRoutingMarkChain}) {
    if (!ModifyChain(IpFamily::Dual, "mangle", "-F", chain))
      LOG(ERROR) << "Failed to flush " << chain << " mangle chain";

    if (!ModifyChain(IpFamily::Dual, "mangle", "-X", chain))
      LOG(ERROR) << "Failed to delete " << chain << " mangle chain";
  }
}

bool Datapath::NetnsAttachName(const std::string& netns_name, pid_t netns_pid) {
  // Try first to delete any netns with name |netns_name| in case patchpanel
  // did not exit cleanly.
  if (process_runner_->ip_netns_delete(netns_name, false /*log_failures*/) == 0)
    LOG(INFO) << "Deleted left over network namespace name " << netns_name;
  return process_runner_->ip_netns_attach(netns_name, netns_pid) == 0;
}

bool Datapath::NetnsDeleteName(const std::string& netns_name) {
  return process_runner_->ip_netns_delete(netns_name) == 0;
}

bool Datapath::AddBridge(const std::string& ifname,
                         uint32_t ipv4_addr,
                         uint32_t ipv4_prefix_len) {
  // Configure the persistent Chrome OS bridge interface with static IP.
  if (process_runner_->brctl("addbr", {ifname}) != 0) {
    return false;
  }

  if (process_runner_->ip(
          "addr", "add",
          {IPv4AddressToCidrString(ipv4_addr, ipv4_prefix_len), "brd",
           IPv4AddressToString(Ipv4BroadcastAddr(ipv4_addr, ipv4_prefix_len)),
           "dev", ifname}) != 0) {
    RemoveBridge(ifname);
    return false;
  }

  if (process_runner_->ip("link", "set", {ifname, "up"}) != 0) {
    RemoveBridge(ifname);
    return false;
  }

  // See nat.conf in chromeos-nat-init for the rest of the NAT setup rules.
  if (!AddOutboundIPv4SNATMark(ifname)) {
    RemoveBridge(ifname);
    return false;
  }

  return true;
}

void Datapath::RemoveBridge(const std::string& ifname) {
  RemoveOutboundIPv4SNATMark(ifname);
  process_runner_->ip("link", "set", {ifname, "down"});
  process_runner_->brctl("delbr", {ifname});
}

bool Datapath::AddToBridge(const std::string& br_ifname,
                           const std::string& ifname) {
  return (process_runner_->brctl("addif", {br_ifname, ifname}) == 0);
}

std::string Datapath::AddTAP(const std::string& name,
                             const MacAddress* mac_addr,
                             const SubnetAddress* ipv4_addr,
                             const std::string& user) {
  base::ScopedFD dev(open(kTunDev, O_RDWR | O_NONBLOCK));
  if (!dev.is_valid()) {
    PLOG(ERROR) << "Failed to open " << kTunDev;
    return "";
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, name.empty() ? kDefaultIfname : name.c_str(),
          sizeof(ifr.ifr_name));
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

  // If a template was given as the name, ifr_name will be updated with the
  // actual interface name.
  if ((*ioctl_)(dev.get(), TUNSETIFF, &ifr) != 0) {
    PLOG(ERROR) << "Failed to create tap interface " << name;
    return "";
  }
  const char* ifname = ifr.ifr_name;

  if ((*ioctl_)(dev.get(), TUNSETPERSIST, 1) != 0) {
    PLOG(ERROR) << "Failed to persist the interface " << ifname;
    return "";
  }

  if (!user.empty()) {
    uid_t uid = -1;
    if (!brillo::userdb::GetUserInfo(user, &uid, nullptr)) {
      PLOG(ERROR) << "Unable to look up UID for " << user;
      RemoveTAP(ifname);
      return "";
    }
    if ((*ioctl_)(dev.get(), TUNSETOWNER, uid) != 0) {
      PLOG(ERROR) << "Failed to set owner " << uid << " of tap interface "
                  << ifname;
      RemoveTAP(ifname);
      return "";
    }
  }

  // Create control socket for configuring the interface.
  base::ScopedFD sock(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!sock.is_valid()) {
    PLOG(ERROR) << "Failed to create control socket for tap interface "
                << ifname;
    RemoveTAP(ifname);
    return "";
  }

  if (ipv4_addr) {
    struct sockaddr_in* addr =
        reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr);
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = static_cast<in_addr_t>(ipv4_addr->Address());
    if ((*ioctl_)(sock.get(), SIOCSIFADDR, &ifr) != 0) {
      PLOG(ERROR) << "Failed to set ip address for vmtap interface " << ifname
                  << " {" << ipv4_addr->ToCidrString() << "}";
      RemoveTAP(ifname);
      return "";
    }

    struct sockaddr_in* netmask =
        reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_netmask);
    netmask->sin_family = AF_INET;
    netmask->sin_addr.s_addr = static_cast<in_addr_t>(ipv4_addr->Netmask());
    if ((*ioctl_)(sock.get(), SIOCSIFNETMASK, &ifr) != 0) {
      PLOG(ERROR) << "Failed to set netmask for vmtap interface " << ifname
                  << " {" << ipv4_addr->ToCidrString() << "}";
      RemoveTAP(ifname);
      return "";
    }
  }

  if (mac_addr) {
    struct sockaddr* hwaddr = &ifr.ifr_hwaddr;
    hwaddr->sa_family = ARPHRD_ETHER;
    memcpy(&hwaddr->sa_data, mac_addr, sizeof(*mac_addr));
    if ((*ioctl_)(sock.get(), SIOCSIFHWADDR, &ifr) != 0) {
      PLOG(ERROR) << "Failed to set mac address for vmtap interface " << ifname
                  << " {" << MacAddressToString(*mac_addr) << "}";
      RemoveTAP(ifname);
      return "";
    }
  }

  if ((*ioctl_)(sock.get(), SIOCGIFFLAGS, &ifr) != 0) {
    PLOG(ERROR) << "Failed to get flags for tap interface " << ifname;
    RemoveTAP(ifname);
    return "";
  }

  ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
  if ((*ioctl_)(sock.get(), SIOCSIFFLAGS, &ifr) != 0) {
    PLOG(ERROR) << "Failed to enable tap interface " << ifname;
    RemoveTAP(ifname);
    return "";
  }

  return ifname;
}

void Datapath::RemoveTAP(const std::string& ifname) {
  process_runner_->ip("tuntap", "del", {ifname, "mode", "tap"});
}

bool Datapath::ConnectVethPair(pid_t netns_pid,
                               const std::string& netns_name,
                               const std::string& veth_ifname,
                               const std::string& peer_ifname,
                               const MacAddress& remote_mac_addr,
                               uint32_t remote_ipv4_addr,
                               uint32_t remote_ipv4_prefix_len,
                               bool remote_multicast_flag) {
  // Set up the virtual pair across the current namespace and |netns_name|.
  if (!AddVirtualInterfacePair(netns_name, veth_ifname, peer_ifname)) {
    LOG(ERROR) << "Failed to create veth pair " << veth_ifname << ","
               << peer_ifname;
    return false;
  }

  // Configure the remote veth in namespace |netns_name|.
  {
    ScopedNS ns(netns_pid);
    if (!ns.IsValid() && netns_pid != kTestPID) {
      LOG(ERROR)
          << "Cannot create virtual link -- invalid container namespace?";
      return false;
    }

    if (!ConfigureInterface(peer_ifname, remote_mac_addr, remote_ipv4_addr,
                            remote_ipv4_prefix_len, true /* link up */,
                            remote_multicast_flag)) {
      LOG(ERROR) << "Failed to configure interface " << peer_ifname;
      RemoveInterface(peer_ifname);
      return false;
    }
  }

  if (!ToggleInterface(veth_ifname, true /*up*/)) {
    LOG(ERROR) << "Failed to bring up interface " << veth_ifname;
    RemoveInterface(veth_ifname);
    return false;
  }

  return true;
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
                                  const MacAddress& mac_addr,
                                  uint32_t ipv4_addr,
                                  uint32_t ipv4_prefix_len,
                                  bool up,
                                  bool enable_multicast) {
  const std::string link = up ? "up" : "down";
  const std::string multicast = enable_multicast ? "on" : "off";
  return (process_runner_->ip(
              "addr", "add",
              {IPv4AddressToCidrString(ipv4_addr, ipv4_prefix_len), "brd",
               IPv4AddressToString(
                   Ipv4BroadcastAddr(ipv4_addr, ipv4_prefix_len)),
               "dev", ifname}) == 0) &&
         (process_runner_->ip("link", "set",
                              {
                                  "dev",
                                  ifname,
                                  link,
                                  "addr",
                                  MacAddressToString(mac_addr),
                                  "multicast",
                                  multicast,
                              }) == 0);
}

void Datapath::RemoveInterface(const std::string& ifname) {
  process_runner_->ip("link", "delete", {ifname}, false /*log_failures*/);
}

bool Datapath::AddSourceIPv4DropRule(const std::string& oif,
                                     const std::string& src_ip) {
  return process_runner_->iptables("filter", {"-I", "OUTPUT", "-o", oif, "-s",
                                              src_ip, "-j", "DROP", "-w"}) == 0;
}

bool Datapath::RemoveSourceIPv4DropRule(const std::string& oif,
                                        const std::string& src_ip) {
  return process_runner_->iptables("filter", {"-D", "OUTPUT", "-o", oif, "-s",
                                              src_ip, "-j", "DROP", "-w"}) == 0;
}

bool Datapath::StartRoutingNamespace(const ConnectedNamespace& nsinfo) {
  // Veth interface configuration and client routing configuration:
  //  - attach a name to the client namespace.
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
          nsinfo.peer_mac_addr, nsinfo.peer_subnet->AddressAtOffset(1),
          nsinfo.peer_subnet->PrefixLength(), false /* enable_multicast */)) {
    LOG(ERROR) << "Failed to create veth pair for"
                  " namespace pid "
               << nsinfo.pid;
    NetnsDeleteName(nsinfo.netns_name);
    return false;
  }

  if (!ConfigureInterface(nsinfo.host_ifname, nsinfo.peer_mac_addr,
                          nsinfo.peer_subnet->AddressAtOffset(0),
                          nsinfo.peer_subnet->PrefixLength(),
                          true /* link up */, false /* enable_multicast */)) {
    LOG(ERROR) << "Cannot configure host interface " << nsinfo.host_ifname;
    RemoveInterface(nsinfo.host_ifname);
    NetnsDeleteName(nsinfo.netns_name);
    return false;
  }

  {
    ScopedNS ns(nsinfo.pid);
    if (!ns.IsValid() && nsinfo.pid != kTestPID) {
      LOG(ERROR) << "Invalid namespace pid " << nsinfo.pid;
      RemoveInterface(nsinfo.host_ifname);
      NetnsDeleteName(nsinfo.netns_name);
      return false;
    }

    if (!AddIPv4Route(nsinfo.peer_subnet->AddressAtOffset(0), INADDR_ANY,
                      INADDR_ANY)) {
      LOG(ERROR) << "Failed to add default /0 route to " << nsinfo.host_ifname
                 << " inside namespace pid " << nsinfo.pid;
      RemoveInterface(nsinfo.host_ifname);
      NetnsDeleteName(nsinfo.netns_name);
      return false;
    }
  }

  // Host namespace routing configuration
  //  - ingress: add route to client subnet via |host_ifname|.
  //  - egress: - allow forwarding for traffic outgoing |host_ifname|.
  //            - add SNAT mark 0x1/0x1 for traffic outgoing |host_ifname|.
  //  Note that by default unsolicited ingress traffic is not forwarded to the
  //  client namespace unless the client specifically set port forwarding
  //  through permission_broker DBus APIs.
  // TODO(hugobenichi) If allow_user_traffic is false, then prevent forwarding
  // both ways between client namespace and other guest containers and VMs.
  uint32_t netmask = Ipv4Netmask(nsinfo.peer_subnet->PrefixLength());
  if (!AddIPv4Route(nsinfo.peer_subnet->AddressAtOffset(0),
                    nsinfo.peer_subnet->BaseAddress(), netmask)) {
    LOG(ERROR) << "Failed to set route to client namespace";
    RemoveInterface(nsinfo.host_ifname);
    NetnsDeleteName(nsinfo.netns_name);
    return false;
  }

  // TODO(b/161508179) Do not rely on legacy fwmark 1 for SNAT.
  if (!AddOutboundIPv4SNATMark(nsinfo.host_ifname)) {
    LOG(ERROR) << "Failed to set SNAT for traffic"
                  " outgoing from "
               << nsinfo.host_ifname;
    RemoveInterface(nsinfo.host_ifname);
    DeleteIPv4Route(nsinfo.peer_subnet->AddressAtOffset(0),
                    nsinfo.peer_subnet->BaseAddress(), netmask);
    StopIpForwarding(IpFamily::IPv4, "", nsinfo.host_ifname);
    NetnsDeleteName(nsinfo.netns_name);
    return false;
  }

  StartRoutingDevice(nsinfo.outbound_ifname, nsinfo.host_ifname,
                     nsinfo.peer_subnet->AddressAtOffset(0), nsinfo.source,
                     nsinfo.route_on_vpn);

  return true;
}

void Datapath::StopRoutingNamespace(const ConnectedNamespace& nsinfo) {
  StopRoutingDevice(nsinfo.outbound_ifname, nsinfo.host_ifname,
                    nsinfo.peer_subnet->AddressAtOffset(0), nsinfo.source,
                    nsinfo.route_on_vpn);
  RemoveInterface(nsinfo.host_ifname);
  RemoveOutboundIPv4SNATMark(nsinfo.host_ifname);
  DeleteIPv4Route(nsinfo.peer_subnet->AddressAtOffset(0),
                  nsinfo.peer_subnet->BaseAddress(),
                  Ipv4Netmask(nsinfo.peer_subnet->PrefixLength()));
  NetnsDeleteName(nsinfo.netns_name);
}

void Datapath::StartRoutingDevice(const std::string& ext_ifname,
                                  const std::string& int_ifname,
                                  uint32_t int_ipv4_addr,
                                  TrafficSource source,
                                  bool route_on_vpn) {
  if (source == TrafficSource::ARC && !ext_ifname.empty() &&
      int_ipv4_addr != 0 &&
      !AddInboundIPv4DNAT(ext_ifname, IPv4AddressToString(int_ipv4_addr)))
    LOG(ERROR) << "Failed to configure ingress traffic rules for " << ext_ifname
               << "->" << int_ifname;

  if (!StartIpForwarding(IpFamily::IPv4, ext_ifname, int_ifname))
    LOG(ERROR) << "Failed to enable IP forwarding for " << ext_ifname << "->"
               << int_ifname;

  if (!StartIpForwarding(IpFamily::IPv4, int_ifname, ext_ifname))
    LOG(ERROR) << "Failed to enable IP forwarding for " << ext_ifname << "<-"
               << int_ifname;

  if (!ModifyFwmarkSourceTag("-A", int_ifname, source))
    LOG(ERROR) << "Failed to add PREROUTING fwmark tagging rule for source "
               << source << " for " << int_ifname;

  if (!ext_ifname.empty()) {
    // If |ext_ifname| is not null, mark egress traffic with the
    // fwmark routing tag corresponding to |ext_ifname|.
    if (!ModifyFwmarkRoutingTag("PREROUTING", "-A", ext_ifname, int_ifname))
      LOG(ERROR) << "Failed to add PREROUTING fwmark routing tag for "
                 << ext_ifname << "<-" << int_ifname;
  } else {
    // Otherwise if ext_ifname is null, set up a CONNMARK restore rule in
    // PREROUTING to apply any fwmark routing tag saved for the current
    // connection, and rely on implicit routing to the default logical network
    // otherwise.
    if (!ModifyConnmarkRestore(IpFamily::Dual, "PREROUTING", "-A", int_ifname,
                               kFwmarkRoutingMask))
      LOG(ERROR) << "Failed to add PREROUTING CONNMARK restore rule for "
                 << int_ifname;

    // Forwarded traffic from downstream virtual devices routed to the system
    // default network is eligible to be routed through a VPN if |route_on_vpn|
    // is true.
    if (route_on_vpn &&
        !ModifyFwmarkVpnJumpRule("PREROUTING", "-A", int_ifname, {}, {}))
      LOG(ERROR) << "Failed to add jump rule to VPN chain for " << int_ifname;
  }
}

void Datapath::StopRoutingDevice(const std::string& ext_ifname,
                                 const std::string& int_ifname,
                                 uint32_t int_ipv4_addr,
                                 TrafficSource source,
                                 bool route_on_vpn) {
  if (source == TrafficSource::ARC && !ext_ifname.empty() && int_ipv4_addr != 0)
    RemoveInboundIPv4DNAT(ext_ifname, IPv4AddressToString(int_ipv4_addr));
  StopIpForwarding(IpFamily::IPv4, ext_ifname, int_ifname);
  StopIpForwarding(IpFamily::IPv4, int_ifname, ext_ifname);
  ModifyFwmarkSourceTag("-D", int_ifname, source);
  if (!ext_ifname.empty()) {
    ModifyFwmarkRoutingTag("PREROUTING", "-D", ext_ifname, int_ifname);
  } else {
    ModifyConnmarkRestore(IpFamily::Dual, "PREROUTING", "-D", int_ifname,
                          kFwmarkRoutingMask);
    if (route_on_vpn)
      ModifyFwmarkVpnJumpRule("PREROUTING", "-D", int_ifname, {}, {});
  }
}

bool Datapath::AddInboundIPv4DNAT(const std::string& ifname,
                                  const std::string& ipv4_addr) {
  // Direct ingress IP traffic to existing sockets.
  if (process_runner_->iptables(
          "nat", {"-A", "PREROUTING", "-i", ifname, "-m", "socket",
                  "--nowildcard", "-j", "ACCEPT", "-w"}) != 0)
    return false;

  // Direct ingress TCP & UDP traffic to ARC interface for new connections.
  if (process_runner_->iptables(
          "nat", {"-A", "PREROUTING", "-i", ifname, "-p", "tcp", "-j", "DNAT",
                  "--to-destination", ipv4_addr, "-w"}) != 0) {
    RemoveInboundIPv4DNAT(ifname, ipv4_addr);
    return false;
  }
  if (process_runner_->iptables(
          "nat", {"-A", "PREROUTING", "-i", ifname, "-p", "udp", "-j", "DNAT",
                  "--to-destination", ipv4_addr, "-w"}) != 0) {
    RemoveInboundIPv4DNAT(ifname, ipv4_addr);
    return false;
  }

  return true;
}

void Datapath::RemoveInboundIPv4DNAT(const std::string& ifname,
                                     const std::string& ipv4_addr) {
  process_runner_->iptables(
      "nat", {"-D", "PREROUTING", "-i", ifname, "-p", "udp", "-j", "DNAT",
              "--to-destination", ipv4_addr, "-w"});
  process_runner_->iptables(
      "nat", {"-D", "PREROUTING", "-i", ifname, "-p", "tcp", "-j", "DNAT",
              "--to-destination", ipv4_addr, "-w"});
  process_runner_->iptables(
      "nat", {"-D", "PREROUTING", "-i", ifname, "-m", "socket", "--nowildcard",
              "-j", "ACCEPT", "-w"});
}

// TODO(b/161507671) Stop relying on the traffic fwmark 1/1 once forwarded
// egress traffic is routed through the fwmark routing tag.
bool Datapath::AddSNATMarkRules() {
  // chromium:1050579: INVALID packets cannot be tracked by conntrack therefore
  // need to be explicitly dropped.
  if (process_runner_->iptables(
          "filter", {"-A", "FORWARD", "-m", "mark", "--mark", "1/1", "-m",
                     "state", "--state", "INVALID", "-j", "DROP", "-w"}) != 0) {
    return false;
  }
  if (process_runner_->iptables(
          "filter", {"-A", "FORWARD", "-m", "mark", "--mark", "1/1", "-j",
                     "ACCEPT", "-w"}) != 0) {
    return false;
  }
  if (process_runner_->iptables(
          "nat", {"-A", "POSTROUTING", "-m", "mark", "--mark", "1/1", "-j",
                  "MASQUERADE", "-w"}) != 0) {
    RemoveSNATMarkRules();
    return false;
  }
  return true;
}

void Datapath::RemoveSNATMarkRules() {
  process_runner_->iptables("nat", {"-D", "POSTROUTING", "-m", "mark", "--mark",
                                    "1/1", "-j", "MASQUERADE", "-w"});
  process_runner_->iptables("filter", {"-D", "FORWARD", "-m", "mark", "--mark",
                                       "1/1", "-j", "ACCEPT", "-w"});
  process_runner_->iptables(
      "filter", {"-D", "FORWARD", "-m", "mark", "--mark", "1/1", "-m", "state",
                 "--state", "INVALID", "-j", "DROP", "-w"});
}

bool Datapath::AddOutboundIPv4SNATMark(const std::string& ifname) {
  return process_runner_->iptables(
             "mangle", {"-A", "PREROUTING", "-i", ifname, "-j", "MARK",
                        "--set-mark", "1/1", "-w"}) == 0;
}

void Datapath::RemoveOutboundIPv4SNATMark(const std::string& ifname) {
  process_runner_->iptables("mangle", {"-D", "PREROUTING", "-i", ifname, "-j",
                                       "MARK", "--set-mark", "1/1", "-w"});
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
  if ((*ioctl_)(sock.get(), SIOCGIFFLAGS, &ifr) < 0) {
    PLOG(WARNING) << "ioctl() failed to get interface flag on " << ifname;
    return false;
  }
  ifr.ifr_flags |= on;
  ifr.ifr_flags &= ~off;
  if ((*ioctl_)(sock.get(), SIOCSIFFLAGS, &ifr) < 0) {
    PLOG(WARNING) << "ioctl() failed to set flag 0x" << std::hex << on
                  << " unset flag 0x" << std::hex << off << " on " << ifname;
    return false;
  }
  return true;
}

bool Datapath::AddIPv6HostRoute(const std::string& ifname,
                                const std::string& ipv6_addr,
                                int ipv6_prefix_len) {
  std::string ipv6_addr_cidr =
      ipv6_addr + "/" + std::to_string(ipv6_prefix_len);

  return process_runner_->ip6("route", "replace",
                              {ipv6_addr_cidr, "dev", ifname}) == 0;
}

void Datapath::RemoveIPv6HostRoute(const std::string& ifname,
                                   const std::string& ipv6_addr,
                                   int ipv6_prefix_len) {
  std::string ipv6_addr_cidr =
      ipv6_addr + "/" + std::to_string(ipv6_prefix_len);

  process_runner_->ip6("route", "del", {ipv6_addr_cidr, "dev", ifname});
}

bool Datapath::AddIPv6Address(const std::string& ifname,
                              const std::string& ipv6_addr) {
  return process_runner_->ip6("addr", "add", {ipv6_addr, "dev", ifname}) == 0;
}

void Datapath::RemoveIPv6Address(const std::string& ifname,
                                 const std::string& ipv6_addr) {
  process_runner_->ip6("addr", "del", {ipv6_addr, "dev", ifname});
}

void Datapath::StartConnectionPinning(const std::string& ext_ifname) {
  int ifindex = FindIfIndex(ext_ifname);
  if (ifindex != 0 && !ModifyIptables(IpFamily::Dual, "mangle",
                                      {"-A", kCheckRoutingMarkChain, "-o",
                                       ext_ifname, "-m", "mark", "!", "--mark",
                                       Fwmark::FromIfIndex(ifindex).ToString() +
                                           "/" + kFwmarkRoutingMask.ToString(),
                                       "-j", "DROP", "-w"}))
    LOG(ERROR) << "Could not set fwmark routing filter rule for " << ext_ifname;

  // Set in CONNMARK the routing tag associated with |ext_ifname|.
  if (!ModifyConnmarkSetPostrouting(IpFamily::Dual, "-A", ext_ifname))
    LOG(ERROR) << "Could not start connection pinning on " << ext_ifname;
  // Save in CONNMARK the source tag for egress traffic of this connection.
  if (!ModifyConnmarkSave(IpFamily::Dual, "POSTROUTING", "-A", ext_ifname,
                          kFwmarkAllSourcesMask))
    LOG(ERROR) << "Failed to add POSTROUTING CONNMARK rule for saving fwmark "
                  "source tag on "
               << ext_ifname;
  // Restore from CONNMARK the source tag for ingress traffic of this connection
  // (returned traffic).
  if (!ModifyConnmarkRestore(IpFamily::Dual, "PREROUTING", "-A", ext_ifname,
                             kFwmarkAllSourcesMask))
    LOG(ERROR) << "Could not setup fwmark source tagging rule for return "
                  "traffic received on "
               << ext_ifname;
}

void Datapath::StopConnectionPinning(const std::string& ext_ifname) {
  int ifindex = FindIfIndex(ext_ifname);
  if (ifindex != 0 && !ModifyIptables(IpFamily::Dual, "mangle",
                                      {"-D", kCheckRoutingMarkChain, "-o",
                                       ext_ifname, "-m", "mark", "!", "--mark",
                                       Fwmark::FromIfIndex(ifindex).ToString() +
                                           "/" + kFwmarkRoutingMask.ToString(),
                                       "-j", "DROP", "-w"}))
    LOG(ERROR) << "Could not remove fwmark routing filter rule for "
               << ext_ifname;

  if (!ModifyConnmarkSetPostrouting(IpFamily::Dual, "-D", ext_ifname))
    LOG(ERROR) << "Could not stop connection pinning on " << ext_ifname;
  if (!ModifyConnmarkSave(IpFamily::Dual, "POSTROUTING", "-D", ext_ifname,
                          kFwmarkAllSourcesMask))
    LOG(ERROR) << "Could not remove POSTROUTING CONNMARK rule for saving "
                  "fwmark source tag on "
               << ext_ifname;
  if (!ModifyConnmarkRestore(IpFamily::Dual, "PREROUTING", "-D", ext_ifname,
                             kFwmarkAllSourcesMask))
    LOG(ERROR) << "Could not remove fwmark source tagging rule for return "
                  "traffic received on "
               << ext_ifname;
}

void Datapath::StartVpnRouting(const std::string& vpn_ifname) {
  if (process_runner_->iptables("nat", {"-A", "POSTROUTING", "-o", vpn_ifname,
                                        "-j", "MASQUERADE", "-w"}) != 0)
    LOG(ERROR) << "Could not set up SNAT for traffic outgoing " << vpn_ifname;
  StartConnectionPinning(vpn_ifname);
  if (!ModifyFwmarkRoutingTag(kApplyVpnMarkChain, "-A", vpn_ifname, ""))
    LOG(ERROR) << "Failed to set up VPN set-mark rule for " << vpn_ifname;
  if (vpn_ifname != kArcBridge)
    StartRoutingDevice(vpn_ifname, kArcBridge, 0 /*no inbound DNAT */,
                       TrafficSource::ARC, true /* route_on_vpn */);
}

void Datapath::StopVpnRouting(const std::string& vpn_ifname) {
  if (vpn_ifname != kArcBridge)
    StopRoutingDevice(vpn_ifname, kArcBridge, 0 /* no inbound DNAT */,
                      TrafficSource::ARC, false /* route_on_vpn */);
  if (!ModifyFwmarkRoutingTag(kApplyVpnMarkChain, "-D", vpn_ifname, ""))
    LOG(ERROR) << "Failed to remove VPN set-mark rule for " << vpn_ifname;
  StopConnectionPinning(vpn_ifname);
  if (process_runner_->iptables("nat", {"-D", "POSTROUTING", "-o", vpn_ifname,
                                        "-j", "MASQUERADE", "-w"}) != 0)
    LOG(ERROR) << "Could not stop SNAT for traffic outgoing " << vpn_ifname;
}

bool Datapath::ModifyConnmarkSetPostrouting(IpFamily family,
                                            const std::string& op,
                                            const std::string& oif) {
  int ifindex = FindIfIndex(oif);
  if (ifindex == 0) {
    PLOG(ERROR) << "if_nametoindex(" << oif << ") failed";
    return false;
  }

  return ModifyConnmarkSet(family, "POSTROUTING", op, oif,
                           Fwmark::FromIfIndex(ifindex), kFwmarkRoutingMask);
}

bool Datapath::ModifyConnmarkSet(IpFamily family,
                                 const std::string& chain,
                                 const std::string& op,
                                 const std::string& oif,
                                 Fwmark mark,
                                 Fwmark mask) {
  if (chain != kApplyVpnMarkChain && (chain != "POSTROUTING" || oif.empty())) {
    LOG(ERROR) << "Invalid arguments chain=" << chain << " oif=" << oif;
    return false;
  }

  std::vector<std::string> args = {op, chain};
  if (!oif.empty()) {
    args.push_back("-o");
    args.push_back(oif);
  }
  args.push_back("-j");
  args.push_back("CONNMARK");
  args.push_back("--set-mark");
  args.push_back(mark.ToString() + "/" + mask.ToString());
  args.push_back("-w");

  return ModifyIptables(family, "mangle", args);
}

bool Datapath::ModifyConnmarkRestore(IpFamily family,
                                     const std::string& chain,
                                     const std::string& op,
                                     const std::string& iif,
                                     Fwmark mask) {
  std::vector<std::string> args = {op, chain};
  if (!iif.empty()) {
    args.push_back("-i");
    args.push_back(iif);
  }
  args.insert(args.end(), {"-j", "CONNMARK", "--restore-mark", "--mask",
                           mask.ToString(), "-w"});
  return ModifyIptables(family, "mangle", args);
}

bool Datapath::ModifyConnmarkSave(IpFamily family,
                                  const std::string& chain,
                                  const std::string& op,
                                  const std::string& oif,
                                  Fwmark mask) {
  std::vector<std::string> args = {op, chain};
  if (!oif.empty()) {
    args.push_back("-o");
    args.push_back(oif);
  }
  args.insert(args.end(), {"-j", "CONNMARK", "--save-mark", "--mask",
                           mask.ToString(), "-w"});
  return ModifyIptables(family, "mangle", args);
}

bool Datapath::ModifyFwmarkRoutingTag(const std::string& chain,
                                      const std::string& op,
                                      const std::string& ext_ifname,
                                      const std::string& int_ifname) {
  int ifindex = FindIfIndex(ext_ifname);
  if (ifindex == 0) {
    PLOG(ERROR) << "if_nametoindex(" << ext_ifname << ") failed";
    return false;
  }

  return ModifyFwmark(IpFamily::Dual, chain, op, int_ifname, "" /*uid_name*/,
                      0 /*classid*/, Fwmark::FromIfIndex(ifindex),
                      kFwmarkRoutingMask);
}

bool Datapath::ModifyFwmarkSourceTag(const std::string& op,
                                     const std::string& iif,
                                     TrafficSource source) {
  return ModifyFwmark(IpFamily::Dual, "PREROUTING", op, iif, "" /*uid_name*/,
                      0 /*classid*/, Fwmark::FromSource(source),
                      kFwmarkAllSourcesMask);
}

bool Datapath::ModifyFwmarkDefaultLocalSourceTag(const std::string& op,
                                                 TrafficSource source) {
  std::vector<std::string> args = {"-A",
                                   kApplyLocalSourceMarkChain,
                                   "-m",
                                   "mark",
                                   "--mark",
                                   "0x0/" + kFwmarkAllSourcesMask.ToString(),
                                   "-j",
                                   "MARK",
                                   "--set-mark",
                                   Fwmark::FromSource(source).ToString() + "/" +
                                       kFwmarkAllSourcesMask.ToString(),
                                   "-w"};
  return ModifyIptables(IpFamily::Dual, "mangle", args);
}

bool Datapath::ModifyFwmarkLocalSourceTag(const std::string& op,
                                          const LocalSourceSpecs& source) {
  if (std::string(source.uid_name).empty() && source.classid == 0)
    return false;

  Fwmark mark = Fwmark::FromSource(source.source_type);
  if (source.is_on_vpn)
    mark = mark | kFwmarkRouteOnVpn;

  return ModifyFwmark(IpFamily::Dual, kApplyLocalSourceMarkChain, op,
                      "" /*iif*/, source.uid_name, source.classid, mark,
                      kFwmarkPolicyMask);
}

bool Datapath::ModifyFwmark(IpFamily family,
                            const std::string& chain,
                            const std::string& op,
                            const std::string& iif,
                            const std::string& uid_name,
                            uint32_t classid,
                            Fwmark mark,
                            Fwmark mask,
                            bool log_failures) {
  std::vector<std::string> args = {op, chain};
  if (!iif.empty()) {
    args.push_back("-i");
    args.push_back(iif);
  }
  if (!uid_name.empty()) {
    args.push_back("-m");
    args.push_back("owner");
    args.push_back("--uid-owner");
    args.push_back(uid_name);
  }
  if (classid != 0) {
    args.push_back("-m");
    args.push_back("cgroup");
    args.push_back("--cgroup");
    args.push_back(base::StringPrintf("0x%08x", classid));
  }
  args.push_back("-j");
  args.push_back("MARK");
  args.push_back("--set-mark");
  args.push_back(mark.ToString() + "/" + mask.ToString());
  args.push_back("-w");

  return ModifyIptables(family, "mangle", args, log_failures);
}

bool Datapath::ModifyIpForwarding(IpFamily family,
                                  const std::string& op,
                                  const std::string& iif,
                                  const std::string& oif,
                                  bool log_failures) {
  if (iif.empty() && oif.empty()) {
    LOG(ERROR) << "Cannot change IP forwarding with no input or output "
                  "interface specified";
    return false;
  }

  std::vector<std::string> args = {op, "FORWARD"};
  if (!iif.empty()) {
    args.push_back("-i");
    args.push_back(iif);
  }
  if (!oif.empty()) {
    args.push_back("-o");
    args.push_back(oif);
  }
  args.push_back("-j");
  args.push_back("ACCEPT");
  args.push_back("-w");

  return ModifyIptables(family, "filter", args, log_failures);
}

bool Datapath::ModifyFwmarkVpnJumpRule(const std::string& chain,
                                       const std::string& op,
                                       const std::string& iif,
                                       Fwmark mark,
                                       Fwmark mask) {
  std::vector<std::string> args = {op, chain};
  if (!iif.empty()) {
    args.push_back("-i");
    args.push_back(iif);
  }
  if (mark.Value() != 0 && mask.Value() != 0) {
    args.push_back("-m");
    args.push_back("mark");
    args.push_back("--mark");
    args.push_back(mark.ToString() + "/" + mask.ToString());
  }
  args.insert(args.end(), {"-j", kApplyVpnMarkChain, "-w"});
  return ModifyIptables(IpFamily::Dual, "mangle", args);
}

bool Datapath::ModifyChain(IpFamily family,
                           const std::string& table,
                           const std::string& op,
                           const std::string& chain,
                           bool log_failures) {
  return ModifyIptables(family, table, {op, chain, "-w"}, log_failures);
}

bool Datapath::ModifyIptables(IpFamily family,
                              const std::string& table,
                              const std::vector<std::string>& argv,
                              bool log_failures) {
  switch (family) {
    case IPv4:
    case IPv6:
    case Dual:
      break;
    default:
      LOG(ERROR) << "Could not execute iptables command " << table
                 << base::JoinString(argv, " ") << ": incorrect IP family "
                 << family;
      return false;
  }

  bool success = true;
  if (family & IpFamily::IPv4)
    success &= process_runner_->iptables(table, argv, log_failures) == 0;
  if (family & IpFamily::IPv6)
    success &= process_runner_->ip6tables(table, argv, log_failures) == 0;
  return success;
}

bool Datapath::StartIpForwarding(IpFamily family,
                                 const std::string& iif,
                                 const std::string& oif) {
  return ModifyIpForwarding(family, "-A", iif, oif);
}

bool Datapath::StopIpForwarding(IpFamily family,
                                const std::string& iif,
                                const std::string& oif) {
  return ModifyIpForwarding(family, "-D", iif, oif);
}

bool Datapath::AddIPv6Forwarding(const std::string& ifname1,
                                 const std::string& ifname2) {
  // Only start Ipv6 forwarding if -C returns false and it had not been
  // started yet.
  if (!ModifyIpForwarding(IpFamily::IPv6, "-C", ifname1, ifname2,
                          false /*log_failures*/) &&
      !StartIpForwarding(IpFamily::IPv6, ifname1, ifname2)) {
    return false;
  }

  if (!ModifyIpForwarding(IpFamily::IPv6, "-C", ifname2, ifname1,
                          false /*log_failures*/) &&
      !StartIpForwarding(IpFamily::IPv6, ifname2, ifname1)) {
    RemoveIPv6Forwarding(ifname1, ifname2);
    return false;
  }

  return true;
}

void Datapath::RemoveIPv6Forwarding(const std::string& ifname1,
                                    const std::string& ifname2) {
  StopIpForwarding(IpFamily::IPv6, ifname1, ifname2);
  StopIpForwarding(IpFamily::IPv6, ifname2, ifname1);
}

bool Datapath::AddIPv4Route(uint32_t gateway_addr,
                            uint32_t addr,
                            uint32_t netmask) {
  struct rtentry route;
  memset(&route, 0, sizeof(route));
  SetSockaddrIn(&route.rt_gateway, gateway_addr);
  SetSockaddrIn(&route.rt_dst, addr & netmask);
  SetSockaddrIn(&route.rt_genmask, netmask);
  route.rt_flags = RTF_UP | RTF_GATEWAY;
  return ModifyRtentry(SIOCADDRT, &route);
}

bool Datapath::DeleteIPv4Route(uint32_t gateway_addr,
                               uint32_t addr,
                               uint32_t netmask) {
  struct rtentry route;
  memset(&route, 0, sizeof(route));
  SetSockaddrIn(&route.rt_gateway, gateway_addr);
  SetSockaddrIn(&route.rt_dst, addr & netmask);
  SetSockaddrIn(&route.rt_genmask, netmask);
  route.rt_flags = RTF_UP | RTF_GATEWAY;
  return ModifyRtentry(SIOCDELRT, &route);
}

bool Datapath::AddIPv4Route(const std::string& ifname,
                            uint32_t addr,
                            uint32_t netmask) {
  struct rtentry route;
  memset(&route, 0, sizeof(route));
  SetSockaddrIn(&route.rt_dst, addr & netmask);
  SetSockaddrIn(&route.rt_genmask, netmask);
  char rt_dev[IFNAMSIZ];
  strncpy(rt_dev, ifname.c_str(), IFNAMSIZ);
  rt_dev[IFNAMSIZ - 1] = '\0';
  route.rt_dev = rt_dev;
  route.rt_flags = RTF_UP | RTF_GATEWAY;
  return ModifyRtentry(SIOCADDRT, &route);
}

bool Datapath::DeleteIPv4Route(const std::string& ifname,
                               uint32_t addr,
                               uint32_t netmask) {
  struct rtentry route;
  memset(&route, 0, sizeof(route));
  SetSockaddrIn(&route.rt_dst, addr & netmask);
  SetSockaddrIn(&route.rt_genmask, netmask);
  char rt_dev[IFNAMSIZ];
  strncpy(rt_dev, ifname.c_str(), IFNAMSIZ);
  rt_dev[IFNAMSIZ - 1] = '\0';
  route.rt_dev = rt_dev;
  route.rt_flags = RTF_UP | RTF_GATEWAY;
  return ModifyRtentry(SIOCDELRT, &route);
}

bool Datapath::ModifyRtentry(ioctl_req_t op, struct rtentry* route) {
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
  if (HANDLE_EINTR(ioctl_(fd.get(), op, route)) != 0) {
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

void Datapath::SetIfnameIndex(const std::string& ifname, int ifindex) {
  if_nametoindex_[ifname] = ifindex;
}

int Datapath::FindIfIndex(const std::string& ifname) {
  uint32_t ifindex = if_nametoindex(ifname.c_str());
  if (ifindex > 0) {
    if_nametoindex_[ifname] = ifindex;
    return ifindex;
  }

  const auto it = if_nametoindex_.find(ifname);
  if (it != if_nametoindex_.end())
    return it->second;

  return 0;
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
         << ", peer_subnet: " << nsinfo.peer_subnet->ToCidrString() << '}';
  return stream;
}

}  // namespace patchpanel
