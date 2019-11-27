// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/datapath.h"

#include <fcntl.h>
#include <linux/if_tun.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/userdb_utils.h>

#include "arc/network/net_util.h"

namespace arc_networkd {

namespace {
constexpr char kDefaultNetmask[] = "255.255.255.252";
constexpr char kDefaultIfname[] = "vmtap%d";
constexpr char kTunDev[] = "/dev/net/tun";
}  // namespace

std::string ArcVethHostName(std::string ifname) {
  return "veth_" + ifname;
}

std::string ArcVethPeerName(std::string ifname) {
  return "peer_" + ifname;
}

Datapath::Datapath(MinijailedProcessRunner* process_runner)
    : Datapath(process_runner, ioctl) {}

Datapath::Datapath(MinijailedProcessRunner* process_runner, ioctl_t ioctl_hook)
    : process_runner_(process_runner), ioctl_(ioctl_hook) {
  CHECK(process_runner_);
}

MinijailedProcessRunner& Datapath::runner() const {
  return *process_runner_;
}

bool Datapath::AddBridge(const std::string& ifname,
                         const std::string& ipv4_addr) {
  // Configure the persistent Chrome OS bridge interface with static IP.
  if (process_runner_->Run({kBrctlPath, "addbr", ifname}) != 0) {
    return false;
  }

  if (process_runner_->Run({kIfConfigPath, ifname, ipv4_addr, "netmask",
                            kDefaultNetmask, "up"}) != 0) {
    RemoveBridge(ifname);
    return false;
  }

  // See nat.conf in chromeos-nat-init for the rest of the NAT setup rules.
  if (process_runner_->Run({kIpTablesPath, "-t", "mangle", "-A", "PREROUTING",
                            "-i", ifname, "-j", "MARK", "--set-mark", "1",
                            "-w"}) != 0) {
    RemoveBridge(ifname);
    return false;
  }

  return true;
}

void Datapath::RemoveBridge(const std::string& ifname) {
  process_runner_->Run({kIpTablesPath, "-t", "mangle", "-D", "PREROUTING", "-i",
                        ifname, "-j", "MARK", "--set-mark", "1", "-w"});
  process_runner_->Run({kIfConfigPath, ifname, "down"});
  process_runner_->Run({kBrctlPath, "delbr", ifname});
}

bool Datapath::AddToBridge(const std::string& br_ifname,
                           const std::string& ifname) {
  return (process_runner_->Run({kBrctlPath, "addif", br_ifname, ifname}) == 0);
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
  process_runner_->Run({kIpPath, "tuntap", "del", ifname, "mode", "tap"});
}

std::string Datapath::AddVirtualBridgedInterface(const std::string& ifname,
                                                 const std::string& mac_addr,
                                                 const std::string& br_ifname) {
  const std::string veth = ArcVethHostName(ifname);
  const std::string peer = ArcVethPeerName(ifname);

  RemoveInterface(veth);
  if (process_runner_->Run({kIpPath, "link", "add", veth, "type", "veth",
                            "peer", "name", peer}) != 0) {
    return "";
  }

  if (process_runner_->Run({kIfConfigPath, veth, "up"}) != 0) {
    RemoveInterface(veth);
    RemoveInterface(peer);
    return "";
  }

  if (process_runner_->Run({kIpPath, "link", "set", "dev", peer, "addr",
                            mac_addr, "down"}) != 0) {
    RemoveInterface(veth);
    RemoveInterface(peer);
    return "";
  }

  if (!AddToBridge(br_ifname, veth)) {
    RemoveInterface(veth);
    RemoveInterface(peer);
    return "";
  }

  return peer;
}

void Datapath::RemoveInterface(const std::string& ifname) {
  process_runner_->Run({kIpPath, "link", "delete", ifname}, false /* log */);
}

bool Datapath::AddInterfaceToContainer(int ns,
                                       const std::string& src_ifname,
                                       const std::string& dst_ifname,
                                       const std::string& dst_ipv4,
                                       bool fwd_multicast) {
  const std::string pid = base::IntToString(ns);
  return (process_runner_->Run(
              {kIpPath, "link", "set", src_ifname, "netns", pid}) == 0) &&
         (process_runner_->AddInterfaceToContainer(src_ifname, dst_ifname,
                                                   dst_ipv4, kDefaultNetmask,
                                                   fwd_multicast, pid) == 0);
}

bool Datapath::AddLegacyIPv4DNAT(const std::string& ipv4_addr) {
  // Forward "unclaimed" packets to Android to allow inbound connections
  // from devices on the LAN.
  if (process_runner_->Run(
          {kIpTablesPath, "-t", "nat", "-N", "dnat_arc", "-w"}) != 0)
    return false;

  if (process_runner_->Run({kIpTablesPath, "-t", "nat", "-A", "dnat_arc", "-j",
                            "DNAT", "--to-destination", ipv4_addr, "-w"}) !=
      0) {
    RemoveLegacyIPv4DNAT();
    return false;
  }

  // This chain is dynamically updated whenever the default interface
  // changes.
  if (process_runner_->Run(
          {kIpTablesPath, "-t", "nat", "-N", "try_arc", "-w"}) != 0) {
    RemoveLegacyIPv4DNAT();
    return false;
  }

  if (process_runner_->Run({kIpTablesPath, "-t", "nat", "-A", "PREROUTING",
                            "-m", "socket", "--nowildcard", "-j", "ACCEPT",
                            "-w"}) != 0) {
    RemoveLegacyIPv4DNAT();
    return false;
  }

  if (process_runner_->Run({kIpTablesPath, "-t", "nat", "-A", "PREROUTING",
                            "-p", "tcp", "-j", "try_arc", "-w"}) != 0) {
    RemoveLegacyIPv4DNAT();
    return false;
  }

  if (process_runner_->Run({kIpTablesPath, "-t", "nat", "-A", "PREROUTING",
                            "-p", "udp", "-j", "try_arc", "-w"}) != 0) {
    RemoveLegacyIPv4DNAT();
    return false;
  }

  return true;
}

void Datapath::RemoveLegacyIPv4DNAT() {
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-D", "PREROUTING", "-p",
                        "udp", "-j", "try_arc", "-w"});
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-D", "PREROUTING", "-p",
                        "tcp", "-j", "try_arc", "-w"});
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-D", "PREROUTING", "-m",
                        "socket", "--nowildcard", "-j", "ACCEPT", "-w"});
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-F", "try_arc", "-w"});
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-X", "try_arc", "-w"});
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-F", "dnat_arc", "-w"});
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-X", "dnat_arc", "-w"});
}

bool Datapath::AddLegacyIPv4InboundDNAT(const std::string& ifname) {
  return (process_runner_->Run({kIpTablesPath, "-t", "nat", "-A", "try_arc",
                                "-i", ifname, "-j", "dnat_arc", "-w"}) != 0);
}

void Datapath::RemoveLegacyIPv4InboundDNAT() {
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-F", "try_arc", "-w"});
}

bool Datapath::AddInboundIPv4DNAT(const std::string& ifname,
                                  const std::string& ipv4_addr) {
  // Direct ingress IP traffic to existing sockets.
  if (process_runner_->Run({kIpTablesPath, "-t", "nat", "-A", "PREROUTING",
                            "-i", ifname, "-m", "socket", "--nowildcard", "-j",
                            "ACCEPT", "-w"}) != 0)
    return false;

  // Direct ingress TCP & UDP traffic to ARC interface for new connections.
  if (process_runner_->Run({kIpTablesPath, "-t", "nat", "-A", "PREROUTING",
                            "-i", ifname, "-p", "tcp", "-j", "DNAT",
                            "--to-destination", ipv4_addr, "-w"}) != 0) {
    RemoveInboundIPv4DNAT(ifname, ipv4_addr);
    return false;
  }
  if (process_runner_->Run({kIpTablesPath, "-t", "nat", "-A", "PREROUTING",
                            "-i", ifname, "-p", "udp", "-j", "DNAT",
                            "--to-destination", ipv4_addr, "-w"}) != 0) {
    RemoveInboundIPv4DNAT(ifname, ipv4_addr);
    return false;
  }

  return true;
}

void Datapath::RemoveInboundIPv4DNAT(const std::string& ifname,
                                     const std::string& ipv4_addr) {
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-D", "PREROUTING", "-i",
                        ifname, "-p", "udp", "-j", "DNAT", "--to-destination",
                        ipv4_addr, "-w"});
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-D", "PREROUTING", "-i",
                        ifname, "-p", "tcp", "-j", "DNAT", "--to-destination",
                        ipv4_addr, "-w"});
  process_runner_->Run({kIpTablesPath, "-t", "nat", "-D", "PREROUTING", "-i",
                        ifname, "-m", "socket", "--nowildcard", "-j", "ACCEPT",
                        "-w"});
}

bool Datapath::AddOutboundIPv4(const std::string& ifname) {
  return process_runner_->Run({kIpTablesPath, "-t", "filter", "-A", "FORWARD",
                               "-o", ifname, "-j", "ACCEPT", "-w"}) == 0;
}

void Datapath::RemoveOutboundIPv4(const std::string& ifname) {
  process_runner_->Run({kIpTablesPath, "-t", "filter", "-D", "FORWARD", "-o",
                        ifname, "-j", "ACCEPT", "-w"});
}

bool Datapath::SetInterfaceFlag(const std::string& ifname, uint16_t flag) {
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
  ifr.ifr_flags |= flag;
  if ((*ioctl_)(sock.get(), SIOCSIFFLAGS, &ifr) < 0) {
    PLOG(WARNING) << "ioctl() failed to set flag 0x" << std::hex << flag
                  << " on " << ifname;
    return false;
  }
  return true;
}

bool Datapath::AddIPv6GatewayRoutes(const std::string& ifname,
                                    const std::string& ipv6_addr,
                                    const std::string& ipv6_router,
                                    int ipv6_prefix_len,
                                    int routing_table) {
  std::string ipv6_addr_cidr =
      ipv6_addr + "/" + std::to_string(ipv6_prefix_len);

  process_runner_->Run(
      {kIpPath, "-6", "addr", "add", ipv6_addr_cidr, "dev", ifname});

  process_runner_->Run({kIpPath, "-6", "route", "add", ipv6_router, "dev",
                        ifname, "table", std::to_string(routing_table)});

  process_runner_->Run({kIpPath, "-6", "route", "add", "default", "via",
                        ipv6_router, "dev", ifname, "table",
                        std::to_string(routing_table)});
  return true;
}

void Datapath::RemoveIPv6GatewayRoutes(const std::string& ifname,
                                       const std::string& ipv6_addr,
                                       const std::string& ipv6_router,
                                       int ipv6_prefix_len,
                                       int routing_table) {
  std::string ipv6_addr_cidr =
      ipv6_addr + "/" + std::to_string(ipv6_prefix_len);

  process_runner_->Run({kIpPath, "-6", "route", "del", "default", "via",
                        ipv6_router, "dev", ifname, "table",
                        std::to_string(routing_table)});
  process_runner_->Run({kIpPath, "-6", "route", "del", ipv6_router, "dev",
                        ifname, "table", std::to_string(routing_table)});
  process_runner_->Run(
      {kIpPath, "-6", "addr", "del", ipv6_addr_cidr, "dev", ifname}, false);
}

bool Datapath::AddIPv6HostRoute(const std::string& ifname,
                                const std::string& ipv6_addr,
                                int ipv6_prefix_len) {
  std::string ipv6_addr_cidr =
      ipv6_addr + "/" + std::to_string(ipv6_prefix_len);

  return process_runner_->Run({kIpPath, "-6", "route", "add", ipv6_addr_cidr,
                               "dev", ifname}) == 0;
}

void Datapath::RemoveIPv6HostRoute(const std::string& ifname,
                                   const std::string& ipv6_addr,
                                   int ipv6_prefix_len) {
  std::string ipv6_addr_cidr =
      ipv6_addr + "/" + std::to_string(ipv6_prefix_len);

  process_runner_->Run(
      {kIpPath, "-6", "route", "del", ipv6_addr_cidr, "dev", ifname});
}

bool Datapath::AddIPv6Neighbor(const std::string& ifname,
                               const std::string& ipv6_addr) {
  return process_runner_->Run({kIpPath, "-6", "neigh", "add", "proxy",
                               ipv6_addr, "dev", ifname}) == 0;
}

void Datapath::RemoveIPv6Neighbor(const std::string& ifname,
                                  const std::string& ipv6_addr) {
  process_runner_->Run(
      {kIpPath, "-6", "neigh", "del", "proxy", ipv6_addr, "dev", ifname});
}

bool Datapath::AddIPv6Forwarding(const std::string& ifname1,
                                 const std::string& ifname2) {
  if (process_runner_->Run({kIp6TablesPath, "-A", "FORWARD", "-i", ifname1,
                            "-o", ifname2, "-j", "ACCEPT", "-w"}) != 0) {
    return false;
  }

  if (process_runner_->Run({kIp6TablesPath, "-A", "FORWARD", "-i", ifname2,
                            "-o", ifname1, "-j", "ACCEPT", "-w"}) != 0) {
    RemoveIPv6Forwarding(ifname1, ifname2);
    return false;
  }

  return true;
}

void Datapath::RemoveIPv6Forwarding(const std::string& ifname1,
                                    const std::string& ifname2) {
  process_runner_->Run({kIp6TablesPath, "-D", "FORWARD", "-i", ifname1, "-o",
                        ifname2, "-j", "ACCEPT", "-w"});

  process_runner_->Run({kIp6TablesPath, "-D", "FORWARD", "-i", ifname2, "-o",
                        ifname1, "-j", "ACCEPT", "-w"});
}

}  // namespace arc_networkd
