// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_DATAPATH_H_
#define ARC_NETWORK_DATAPATH_H_

#include <string>

#include <base/macros.h>

#include "arc/network/mac_address_generator.h"
#include "arc/network/minijailed_process_runner.h"
#include "arc/network/subnet.h"

namespace arc_networkd {

// cros lint will yell to force using int16/int64 instead of long here, however
// note that unsigned long IS the correct signature for ioctl in Linux kernel -
// it's 32 bits on 32-bit platform and 64 bits on 64-bit one.
using ioctl_req_t = unsigned long;
typedef int (*ioctl_t)(int, ioctl_req_t, ...);

// Returns for given interface name the host name of a ARC veth pair.
std::string ArcVethHostName(std::string ifname);

// Returns for given interface name the peer name of a ARC veth pair.
std::string ArcVethPeerName(std::string ifname);

// ARC networking data path configuration utility.
// IPV4 addresses are always specified in singular dotted-form (a.b.c.d)
// (not in CIDR representation
class Datapath {
 public:
  // |process_runner| must not be null; it is not owned.
  explicit Datapath(MinijailedProcessRunner* process_runner);
  // Provided for testing only.
  Datapath(MinijailedProcessRunner* process_runner, ioctl_t ioctl_hook);
  virtual ~Datapath() = default;

  virtual bool AddBridge(const std::string& ifname,
                         const std::string& ipv4_addr);
  virtual void RemoveBridge(const std::string& ifname);

  virtual bool AddToBridge(const std::string& br_ifname,
                           const std::string& ifname);

  // Adds a new TAP device.
  // |name| may be empty, in which case a default device name will be used;
  // it may be a template (e.g. vmtap%d), in which case the kernel will
  // generate the name; or it may be fully defined. In all cases, upon success,
  // the function returns the actual name of the interface.
  // |mac_addr| and |ipv4_addr| should be null if this interface will be later
  // bridged.
  // If |user| is empty, no owner will be set
  virtual std::string AddTAP(const std::string& name,
                             const MacAddress* mac_addr,
                             const SubnetAddress* ipv4_addr,
                             const std::string& user);

  // |ifname| must be the actual name of the interface.
  virtual void RemoveTAP(const std::string& ifname);

  // The following are iptables methods.
  // When specified, |ipv4_addr| is always singlar dotted-form (a.b.c.d)
  // IPv4 address (not a CIDR representation).

  // Creates a virtual interface and adds one side to bridge |br_ifname| and
  // returns the name of the other side.
  virtual std::string AddVirtualBridgedInterface(const std::string& ifname,
                                                 const std::string& mac_addr,
                                                 const std::string& br_ifname);
  virtual void RemoveInterface(const std::string& ifname);

  // Inject an interace into the ARC++ container namespace.
  virtual bool AddInterfaceToContainer(int ns,
                                       const std::string& src_ifname,
                                       const std::string& dst_ifname,
                                       const std::string& dst_ipv4,
                                       bool fwd_multicast);
  // Create (or flush and delete) pre-routing rules supporting legacy (ARC N)
  // single networking DNAT configuration.
  virtual bool AddLegacyIPv4DNAT(const std::string& ipv4_addr);
  virtual void RemoveLegacyIPv4DNAT();

  // Enable ingress traffic from a specific physical device to the legacy
  // single networkng DNAT configuration.
  virtual bool AddLegacyIPv4InboundDNAT(const std::string& ifname);
  virtual void RemoveLegacyIPv4InboundDNAT();

  // Create (or delete) pre-routing rules allowing direct ingress on |ifname|
  // to guest desintation |ipv4_addr|.
  virtual bool AddInboundIPv4DNAT(const std::string& ifname,
                                  const std::string& ipv4_addr);
  virtual void RemoveInboundIPv4DNAT(const std::string& ifname,
                                     const std::string& ipv4_addr);

  // Create (or delete) a forwarding rule for |ifname|.
  virtual bool AddOutboundIPv4(const std::string& ifname);
  virtual void RemoveOutboundIPv4(const std::string& ifname);

  // Methods supporting IPv6 configuration for ARC.
  virtual bool SetInterfaceFlag(const std::string& ifname, uint16_t flag);

  virtual bool AddIPv6Forwarding(const std::string& ifname1,
                                 const std::string& ifname2);
  virtual void RemoveIPv6Forwarding(const std::string& ifname1,
                                    const std::string& ifname2);

  // Methods supporting the legacy IPv6 hacks for ARC.
  virtual bool AddIPv6GatewayRoutes(const std::string& ifname,
                                    const std::string& ipv6_addr,
                                    const std::string& ipv6_router,
                                    int ipv6_prefix_len,
                                    int routing_table);
  virtual void RemoveIPv6GatewayRoutes(const std::string& ifname,
                                       const std::string& ipv6_addr,
                                       const std::string& ipv6_router,
                                       int ipv6_prefix_len,
                                       int routing_table);

  virtual bool AddIPv6HostRoute(const std::string& ifname,
                                const std::string& ipv6_addr,
                                int ipv6_prefix_len);
  virtual void RemoveIPv6HostRoute(const std::string& ifname,
                                   const std::string& ipv6_addr,
                                   int ipv6_prefix_len);

  virtual bool AddIPv6Neighbor(const std::string& ifname,
                               const std::string& ipv6_addr);
  virtual void RemoveIPv6Neighbor(const std::string& ifname,
                                  const std::string& ipv6_addr);

  MinijailedProcessRunner& runner() const;

 private:
  MinijailedProcessRunner* process_runner_;
  ioctl_t ioctl_;

  DISALLOW_COPY_AND_ASSIGN(Datapath);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_DATAPATH_H_
