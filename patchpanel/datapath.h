// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DATAPATH_H_
#define PATCHPANEL_DATAPATH_H_

#include <net/route.h>
#include <sys/types.h>

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/functional/callback_helpers.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/mac_address.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/downstream_network_info.h"
#include "patchpanel/firewall.h"
#include "patchpanel/iptables.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/subnet.h"
#include "patchpanel/system.h"

namespace patchpanel {

// filter INPUT chain for ingress port access rules controlled by
// permission_broker.
constexpr char kIngressPortFirewallChain[] = "ingress_port_firewall";
// filter OUTPUT chain for egress port restriction rules controlled by
// permission_broker.
constexpr char kEgressPortFirewallChain[] = "egress_port_firewall";
// nat PREROUTING chain for ingress DNAT forwarding rules controlled by
// permission_broker.
constexpr char kIngressPortForwardingChain[] = "ingress_port_forwarding";

// Struct holding static IPv6 configuration for ConnectedNamespace when NAT66 is
// used. |host_cidr| and |peer_cidr| prefix CIDR must be the same.
struct StaticIPv6Config {
  // IPv6 CIDR of the "local" veth interface visible on the host namespace.
  net_base::IPv6CIDR host_cidr;
  // IPv6 CIDR of the "remote" veth interface.
  net_base::IPv6CIDR peer_cidr;
};

// Struct holding parameters for Datapath::StartRoutingNamespace requests.
struct ConnectedNamespace {
  // The special pid which indicates this namespace is not attached to an
  // associated process but should be/was created by `ip netns add`.
  static constexpr pid_t kNewNetnsPid = -1;

  // The pid of the client network namespace.
  pid_t pid;
  // The name attached to the client network namespace.
  std::string netns_name;
  // Source to which traffic from |host_ifname| will be attributed.
  TrafficSource source;
  // Interface name of the shill Device for routing outbound traffic from the
  // client namespace. Empty if outbound traffic should be forwarded to the
  // highest priority network (physical or virtual).
  std::string outbound_ifname;
  // If |outbound_ifname| is empty and |route_on_vpn| is false, the traffic from
  // the client namespace will be routed to the highest priority physical
  // network. If |outbound_ifname| is empty and |route_on_vpn| is true, the
  // traffic will be routed through VPN connections. If |outbound_ifname|
  // specifies a valid physical interface, |route_on_vpn| is ignored.
  bool route_on_vpn;
  // Name of the "local" veth interface visible on the host namespace.
  std::string host_ifname;
  // Name of the "remote" veth interface moved into the client namespace.
  std::string peer_ifname;
  // IPv4 subnet assigned to the client namespace.
  std::unique_ptr<Subnet> peer_ipv4_subnet;
  // IPv4 CIDR of the "local" veth interface visible on the host namespace.
  net_base::IPv4CIDR host_ipv4_cidr;
  // IPv4 CIDR of the "remote" veth interface.
  net_base::IPv4CIDR peer_ipv4_cidr;
  // Static IPv6 addresses allocated for ConnectNamespace. Only valid if NAT66
  // is used.
  std::optional<StaticIPv6Config> static_ipv6_config;
  // MAC address of the "local" veth interface visible on the host namespace.
  net_base::MacAddress host_mac_addr;
  // MAC address of the "remote" veth interface.
  net_base::MacAddress peer_mac_addr;
  // shill Device for routing outbound traffic from the client namespace. The
  // Device selected matches |outbound_ifname| if it is defined in the original
  // request, otherwise it matches the default logical or physical Device
  // depending on |route_on_vpn| and depending if a default Device exists.
  std::optional<ShillClient::Device> current_outbound_device;
  // Closure to cancel lifeline FD tracking the file descriptor committed by the
  // DBus client.
  base::ScopedClosureRunner cancel_lifeline_fd;
};

// Describes a DNS DNAT redirection rule issued by dns-proxy.
struct DnsRedirectionRule {
  patchpanel::SetDnsRedirectionRuleRequest::RuleType type;
  std::string input_ifname;
  net_base::IPAddress proxy_address;
  std::vector<net_base::IPAddress> nameservers;
  std::string host_ifname;
  // Closure to cancel lifeline FD tracking the file descriptor committed by the
  // DBus client.
  base::ScopedClosureRunner cancel_lifeline_fd;
};

// Simple enum for specifying a set of IP family values.
enum class IpFamily {
  kIPv4,
  kIPv6,
  kDual,
};

// List of possible guest targets for automatic forwarding rules applied to
// unsolicited ingress traffic not accepted on the host.
enum class AutoDNATTarget {
  kArc,
  kCrostini,
  kParallels,
};

enum class DeviceMode {
  kTun,
  kTap,
};

// ARC networking data path configuration utility.
// IPV4 addresses are always specified in singular dotted-form (a.b.c.d)
// (not in CIDR representation
// Create the initial iptables setup needed for forwarding traffic from VMs and
// containers and for fwmark based routing when the instance is constructed, and
// destroy the setup when the instance is destructed.
class Datapath {
 public:
  explicit Datapath(System* system);
  // Provided for testing only.
  Datapath(MinijailedProcessRunner* process_runner,
           Firewall* firewall,
           System* system);
  Datapath(const Datapath&) = delete;
  Datapath& operator=(const Datapath&) = delete;

  virtual ~Datapath();

  // Attaches the name |netns_name| to a network namespace identified by
  // |netns_pid|. If |netns_pid| is -1, a new namespace with name |netns_name|
  // will be created instead. If |netns_name| had already been created, it will
  // be deleted first.
  virtual bool NetnsAttachName(std::string_view netns_name, pid_t netns_pid);

  // Deletes the name |netns_name| of a network namespace.
  virtual bool NetnsDeleteName(std::string_view netns_name);

  virtual bool AddBridge(std::string_view ifname,
                         const net_base::IPv4CIDR& cidr);
  virtual void RemoveBridge(std::string_view ifname);

  virtual bool AddToBridge(std::string_view br_ifname, std::string_view ifname);

  // Adds a new TUN device or a TAP device.
  // |name| may be empty, in which case a default device name will be used;
  // it may be a template (e.g. vmtap%d), in which case the kernel will
  // generate the name; or it may be fully defined. In all cases, upon success,
  // the function returns the actual name of the interface.
  // |mac_addr| and |ipv4_cidr| should be std::nullopt if this interface will be
  // later bridged.
  // If |user| is empty, no owner will be set.
  virtual std::string AddTunTap(
      std::string_view name,
      const std::optional<net_base::MacAddress>& mac_addr,
      const std::optional<net_base::IPv4CIDR>& ipv4_cidr,
      std::string_view user,
      DeviceMode dev_mode);
  virtual void RemoveTunTap(std::string_view ifname, DeviceMode dev_mode);

  // The following are iptables methods.
  // When specified, |ipv4_addr| is always singlar dotted-form (a.b.c.d)
  // IPv4 address (not a CIDR representation).

  // Creates a virtual interface pair split across the current namespace and the
  // namespace corresponding to |pid|, and create the remote interface
  // |peer_ifname| according to the given parameters. |peer_ifname| is
  // initialized to be up when |up| is true.
  virtual bool ConnectVethPair(
      pid_t pid,
      std::string_view netns_name,
      std::string_view veth_ifname,
      std::string_view peer_ifname,
      net_base::MacAddress remote_mac_addr,
      const net_base::IPv4CIDR& remote_ipv4_cidr,
      const std::optional<net_base::IPv6CIDR>& remote_ipv6_cidr,
      bool remote_multicast_flag,
      bool up = true);

  // Disable and re-enable IPv6.
  virtual void RestartIPv6();

  virtual void RemoveInterface(std::string_view ifname);

  // Create an OUTPUT DROP rule for any locally originated traffic
  // whose src IPv4 matches |src_ip| and would exit |oif|. This is mainly used
  // for dropping Chrome webRTC traffic incorrectly bound on ARC and other
  // guests virtual interfaces (chromium:898210).
  virtual bool AddSourceIPv4DropRule(std::string_view oif,
                                     std::string_view src_ip);

  // Creates a virtual ethernet interface pair shared with the client namespace
  // of |nsinfo.pid| and sets up routing outside and inside the client namespace
  // for connecting the client namespace to the network.
  bool StartRoutingNamespace(const ConnectedNamespace& nsinfo);
  // Destroys the virtual ethernet interface, routing, and network namespace
  // name set for |nsinfo.netns_name| by StartRoutingNamespace. The default
  // route set inside the |nsinfo.netns_name| by patchpanel is not destroyed and
  // it is assumed the client will teardown the namespace.
  void StopRoutingNamespace(const ConnectedNamespace& nsinfo);

  // Start or stop DNS traffic redirection to DNS proxy. The rules created
  // depend on the type requested.
  bool StartDnsRedirection(const DnsRedirectionRule& rule);
  void StopDnsRedirection(const DnsRedirectionRule& rule);

  // Sets up IPv4 SNAT, IPv4 forwarding, and traffic marking for the given
  // downstream network interface |int_ifname| associated to |source|. Traffic
  // from the downstream interface is routed to the shill Device |shill_device|
  // regardless of the current default network selection.
  // IPv6 SNAT and IPv6 forwarding is optionally set up depending on
  // |static_ipv6|.
  virtual void StartRoutingDevice(const ShillClient::Device& shill_device,
                                  std::string_view int_ifname,
                                  TrafficSource source,
                                  bool static_ipv6 = false);

  // Sets up IPv4 SNAT, IPv4 forwarding, and traffic marking for the given
  // downstream network interface |int_ifname| associated to |source|.
  // Traffic from that downstream interface is implicitly routed through the
  // highest priority physical network, follows "system traffic" semantics, and
  // ignores VPN connections.
  // IPv6 SNAT and IPv6 forwarding is optionally set up depending on
  // |static_ipv6|.
  void StartRoutingDeviceAsSystem(std::string_view int_ifname,
                                  TrafficSource source,
                                  bool static_ipv6 = false);

  // Sets up IPv4 SNAT, IPv4 forwarding, and traffic marking for the given
  // downstream network interface |int_ifname| associated to |source|.
  // Traffic from the downstream interface follows "user traffic" semantics and
  // is implicitly routed through the highest priority logical network which can
  // be a VPN connection or the highest priority physical network. If
  // |int_ifname| is associated to a connected namespace and a VPN is connected,
  // an additional IPv4 VPN fwmark tagging bypass rule is needed to allow return
  // traffic to reach to the IPv4 local source. |peer_ipv4_addr| is the address
  // of the interface inside the connected namespace needed to create this rule.
  // If |peer_ipv4_addr| is undefined, no additional rule will be added.
  // IPv6 SNAT, IPv6 forwarding, and IPv6 VPN fwmark tagging bypass rule are
  // optionally set up depending on |int_ipv6_addr| and |peer_ipv6_addr|.
  virtual void StartRoutingDeviceAsUser(
      std::string_view int_ifname,
      TrafficSource source,
      const net_base::IPv4Address& int_ipv4_addr,
      std::optional<net_base::IPv4Address> peer_ipv4_addr = std::nullopt,
      std::optional<net_base::IPv6Address> int_ipv6_addr = std::nullopt,
      std::optional<net_base::IPv6Address> peer_ipv6_addr = std::nullopt);

  // Removes IPv4 iptables, IP forwarding, and traffic marking rules for the
  // given downstream network interface |int_ifname| associated to |source|.
  virtual void StopRoutingDevice(std::string_view int_ifname,
                                 TrafficSource source);

  // Starts or stops marking conntrack entries routed to |shill_device| with its
  // associated fwmark routing tag. Once a conntrack entry is marked with the
  // fwmark routing tag of an upstream network interface, the connection will be
  // pinned to that network interface if conntrack fwmark restore is set for the
  // source.
  virtual void StartConnectionPinning(const ShillClient::Device& shill_device);
  virtual void StopConnectionPinning(const ShillClient::Device& shill_device);
  // Starts or stops VPN routing for:
  //  - Local traffic from sockets of binaries running under uids eligible to be
  //  routed
  //    through VPN connections. These uids are defined by |kLocalSourceTypes|
  //    in routing_service.h
  //  - Forwarded traffic from downstream network interfaces tracking the
  //  default network.
  virtual void StartVpnRouting(const ShillClient::Device& vpn_device);
  virtual void StopVpnRouting(const ShillClient::Device& vpn_device);

  // Starts and stops VPN lockdown mode. When patchpanel VPN lockdown is enabled
  // and no VPN connection exists, any non-ARC traffic that would be routed to a
  // VPN connection is instead rejected in iptables. ARC traffic is ignored
  // because Android already implements VPN lockdown.
  virtual void SetVpnLockdown(bool enable_vpn_lockdown);

  // Start, stop and update IPv6 prefix enforcement on cellular network, so the
  // egress traffic using a source address not in current assigned prefix
  // (usually a leftover address from previous connection) will be dropped.
  virtual void StartSourceIPv6PrefixEnforcement(
      const ShillClient::Device& shill_device);
  virtual void StopSourceIPv6PrefixEnforcement(
      const ShillClient::Device& shill_device);
  virtual void UpdateSourceEnforcementIPv6Prefix(
      const ShillClient::Device& shill_device,
      const std::vector<net_base::IPv6CIDR>& ipv6_addresses);

  // Configures IPv4 interface parameters, IP forwarding rules, and traffic
  // marking for the downstream network interface specified in |info|. Exact
  // firewall rules being configured depend on the DownstreamNetworkTopology
  // value specified in |info|. If the downstream network interface is used in
  // tethering, IPv4 SNAT is also configured with the upstream.
  virtual bool StartDownstreamNetwork(const DownstreamNetworkInfo& info);
  // Clears IPv4 interface parameters, IPv4 SNAT, IP forwarding rules, and
  // traffic marking previously configured with StartDownstreamNetwork.
  virtual void StopDownstreamNetwork(const DownstreamNetworkInfo& info);

  // Methods supporting IPv6 configuration for ARC.
  virtual bool MaskInterfaceFlags(std::string_view ifname,
                                  uint16_t on,
                                  uint16_t off = 0);

  virtual bool AddIPv6HostRoute(
      std::string_view ifname,
      const net_base::IPv6CIDR& ipv6_cidr,
      const std::optional<net_base::IPv6Address>& src_addr = std::nullopt);
  virtual void RemoveIPv6HostRoute(const net_base::IPv6CIDR& ipv6_cidr);

  // Add an 'ip -6 neigh proxy' entry so that |ipv6_addr| is resolvable into MAC
  // by neighbors from |ifname|, though itself is actually configured on a
  // different interface.
  virtual bool AddIPv6NeighborProxy(std::string_view ifname,
                                    const net_base::IPv6Address& ipv6_addr);
  virtual void RemoveIPv6NeighborProxy(std::string_view ifname,
                                       const net_base::IPv6Address& ipv6_addr);

  virtual bool AddIPv6Address(std::string_view ifname,
                              std::string_view ipv6_addr);
  virtual void RemoveIPv6Address(std::string_view ifname,
                                 std::string_view ipv6_addr);

  virtual bool AddIPv4RouteToTable(std::string_view ifname,
                                   const net_base::IPv4CIDR& ipv4_cidr,
                                   int table_id);

  virtual void DeleteIPv4RouteFromTable(std::string_view ifname,
                                        const net_base::IPv4CIDR& ipv4_cidr,
                                        int table_id);

  // Adds (or deletes) a route to direct to |gateway_addr| the traffic destined
  // to the subnet defined by |subnet_cidr|.
  virtual bool AddIPv4Route(const net_base::IPv4Address& gateway_addr,
                            const net_base::IPv4CIDR& subnet_cidr);
  virtual bool DeleteIPv4Route(const net_base::IPv4Address& gateway_addr,
                               const net_base::IPv4CIDR& subnet_cidr);
  virtual bool AddIPv6Route(const net_base::IPv6Address& gateway_addr,
                            const net_base::IPv6CIDR& subnet_cidr);
  virtual bool DeleteIPv6Route(const net_base::IPv6Address& gateway_addr,
                               const net_base::IPv6CIDR& subnet_cidr);

  // Adds (or deletes) an iptables rule for ADB port forwarding.
  virtual bool AddAdbPortForwardRule(std::string_view ifname);
  virtual void DeleteAdbPortForwardRule(std::string_view ifname);

  // Adds (or deletes) an iptables rule for ADB port access.
  virtual bool AddAdbPortAccessRule(std::string_view ifname);
  virtual void DeleteAdbPortAccessRule(std::string_view ifname);

  // Enables or disables netfilter conntrack helpers.
  virtual bool SetConntrackHelpers(bool enable_helpers);
  // Allows (or stops allowing) loopback IPv4 addresses as valid sources or
  // destinations during IPv4 routing for |ifname|. This lets connections
  // originated from guests like ARC or Crostini be accepted on the host and
  // should be used carefully in conjunction with firewall port access rules to
  // only allow very specific connection patterns.
  virtual bool SetRouteLocalnet(std::string_view ifname, bool enable);
  // Adds all |modules| into the kernel using modprobe.
  virtual bool ModprobeAll(const std::vector<std::string>& modules);

  // Create (or delete) DNAT rules for sending unsolicited traffic inbound on
  // interface |ifname| to |ipv4_addr| using the nat PREROUTING subchain
  // associated with |auto_dnat_target|. These rules allow inbound connections
  // to transparently reach Android Apps listening on a network port inside ARC
  // or Linux binaries listening on a network port inside Crostini.
  virtual void AddInboundIPv4DNAT(AutoDNATTarget auto_dnat_target,
                                  const ShillClient::Device& shill_device,
                                  const net_base::IPv4Address& ipv4_addr);
  virtual void RemoveInboundIPv4DNAT(AutoDNATTarget auto_dnat_target,
                                     const ShillClient::Device& shill_device,
                                     const net_base::IPv4Address& ipv4_addr);

  // Enable (or disable) QoS detection (i.e., setting QoS-related bits in
  // fwmark) by modifying the jump rules to the qos_detect chain. These two
  // functions are called by QoSService.
  virtual void EnableQoSDetection();
  virtual void DisableQoSDetection();

  // Enable (or disable) applying DSCP fields in egress packets for QoS, by
  // modifying the jump rules to the qos_apply_dscp chain. These two functions
  // are called by QoSService.
  virtual void EnableQoSApplyingDSCP(std::string_view ifname);
  virtual void DisableQoSApplyingDSCP(std::string_view ifname);

  // Update the QoS detection rules for DoH providers when the list is changed.
  virtual void UpdateDoHProvidersForQoS(
      IpFamily family,
      const std::vector<net_base::IPAddress>& doh_provider_ips);

  // Add (or remove) a QoS detection rule for egress packets from the Borealis
  // VM which enter host via tap interface |ifname|.
  virtual void AddBorealisQoSRule(std::string_view ifname);
  virtual void RemoveBorealisQoSRule(std::string_view ifname);

  // Returns true if the chain |name| exists in |table|.
  virtual bool CheckChain(IpFamily family,
                          Iptables::Table table,
                          std::string_view name);
  // Add, remove, or flush chain |chain| in table |table|.
  virtual bool AddChain(IpFamily family,
                        Iptables::Table table,
                        std::string_view name);
  virtual bool RemoveChain(IpFamily family,
                           Iptables::Table table,
                           std::string_view name);
  virtual bool FlushChain(IpFamily family,
                          Iptables::Table table,
                          std::string_view name);
  // Manipulates a chain |chain| in table |table|.
  virtual bool ModifyChain(IpFamily family,
                           Iptables::Table table,
                           Iptables::Command command,
                           std::string_view chain,
                           bool log_failures = true);
  // Manipulates rules of the FORWARD chain in filter table that accept incoming
  // packets from and outgoing packets to interface |ifname|. The manipulated
  // rules only affect IPv6 packets.
  virtual bool ModifyClatAcceptRules(Iptables::Command command,
                                     std::string_view ifname);
  // Sends an iptables command for table |table|.
  virtual bool ModifyIptables(IpFamily family,
                              Iptables::Table table,
                              Iptables::Command command,
                              std::string_view chain,
                              const std::vector<std::string_view>& argv,
                              bool log_failures = true);
  // Dumps the iptables chains rules for the table |table|. |family| must be
  // either IPv4 or IPv6.
  virtual std::string DumpIptables(IpFamily family, Iptables::Table table);

  // Changes firewall rules based on |request|, allowing ingress traffic to a
  // port, forwarding ingress traffic to a port into ARC or Crostini, or
  // restricting localhost ports for listen(). This function corresponds to
  // the ModifyPortRule method of patchpanel DBus API.
  virtual bool ModifyPortRule(const patchpanel::ModifyPortRuleRequest& request);

 private:
  void Start();
  void Stop();

  // Creates a virtual interface pair.
  bool AddVirtualInterfacePair(std::string_view netns_name,
                               std::string_view veth_ifname,
                               std::string_view peer_ifname);
  // Sets the configuration of an interface. |mac_addr| is an optional argument
  // that allows controlling the MAC address when configuring a virtual
  // interface used for ARC, crosvm, or with a network namespace. |mac_addr|
  // should be left undefined when configuring a physical interface used for a
  // downstream network.
  bool ConfigureInterface(std::string_view ifname,
                          std::optional<net_base::MacAddress> mac_addr,
                          const net_base::IPv4CIDR& ipv4_cidr,
                          const std::optional<net_base::IPv6CIDR>& ipv6_cidr,
                          bool up,
                          bool enable_multicast);
  // Sets the link status.
  bool ToggleInterface(std::string_view ifname, bool up);

  // Creates the base FORWARD filter rules and PREROUTING mangle rules for
  // any downstream network interface (ARC, Crostini, Borealis, Parallels,
  // Bruschetta, ConnectNamespace, Tethering, LocalOnlyNetwork).
  // TODO(b/273749806): Create abstraction to represent the different types of
  // isolation in the FORWARD chain and in the OUTPUT chain instead of relying
  // on inspecting |source|, |int_ifname|, and |upstream_ifname|.
  void AddDownstreamInterfaceRules(
      std::optional<ShillClient::Device> upstream_device,
      std::string_view int_ifname,
      TrafficSource source,
      bool static_ipv6 = false);

  // Allows traffic to ConnectedNamespace network to skip VPN rule.
  bool ModifyConnectNamespaceSkipVpnRule(Iptables::Command command,
                                         std::string_view ifname);

  bool ModifyRedirectDnsJumpRule(IpFamily family,
                                 Iptables::Command command,
                                 std::string_view chain,
                                 std::string_view ifname,
                                 std::string_view target_chain,
                                 Fwmark mark = {},
                                 Fwmark mask = {},
                                 bool redirect_on_mark = false);
  bool ModifyDnsRedirectionSkipVpnRule(IpFamily family,
                                       const DnsRedirectionRule& rule,
                                       Iptables::Command command);

  // Allows guest traffic to go to DNS proxy daemon listening on the TAP or
  // bridge interface |iif|.
  bool ModifyIngressDnsProxyAcceptRule(IpFamily family,
                                       Iptables::Command op,
                                       std::string_view iif);

  // Allows traffic to go to DNS proxy's addresses.
  bool ModifyDnsProxyAcceptRule(IpFamily family,
                                const DnsRedirectionRule& rule,
                                Iptables::Command op);

  // Create (or delete) rules to exclude DNS traffic with destination not equal
  // to the proxy's IP in |rule|.
  bool ModifyDnsExcludeDestinationRule(IpFamily family,
                                       const DnsRedirectionRule& rule,
                                       Iptables::Command command,
                                       std::string_view chain);

  // Create (or delete) DNAT rules for redirecting DNS queries to a DNS proxy.
  bool ModifyDnsProxyDNAT(IpFamily family,
                          const DnsRedirectionRule& rule,
                          Iptables::Command command,
                          std::string_view ifname,
                          std::string_view chain);

  bool ModifyConnmarkSet(IpFamily family,
                         std::string_view chain,
                         Iptables::Command command,
                         Fwmark mark,
                         Fwmark mask);
  bool ModifyConnmarkRestore(IpFamily family,
                             std::string_view chain,
                             Iptables::Command command,
                             std::string_view iif,
                             Fwmark mask,
                             bool skip_on_non_empty_mark = false);
  bool ModifyConnmarkSave(IpFamily family,
                          std::string_view chain,
                          Iptables::Command command,
                          Fwmark mask);
  bool ModifyFwmarkRoutingTag(std::string_view chain,
                              Iptables::Command command,
                              Fwmark routing_mark);
  bool ModifyFwmarkSourceTag(std::string_view chain,
                             Iptables::Command command,
                             TrafficSource source);
  bool ModifyFwmark(IpFamily family,
                    std::string_view chain,
                    Iptables::Command command,
                    std::string_view iif,
                    std::string_view uid_name,
                    uint32_t classid,
                    Fwmark mark,
                    Fwmark mask,
                    bool log_failures = true);
  bool ModifyJumpRule(IpFamily family,
                      Iptables::Table table,
                      Iptables::Command command,
                      std::string_view chain,
                      std::string_view target,
                      std::string_view iif,
                      std::string_view oif,
                      bool log_failures = true);
  bool ModifyFwmarkVpnJumpRule(std::string_view chain,
                               Iptables::Command command,
                               Fwmark mark,
                               Fwmark mask);
  bool ModifyIPv4Rtentry(ioctl_req_t op, struct rtentry* route);
  bool ModifyIPv6Rtentry(ioctl_req_t op, struct in6_rtmsg* route);

  // Changing jump rules to qos_detect and qos_apply_dscp chains.
  void ModifyQoSDetectJumpRule(Iptables::Command command);
  void ModifyQoSApplyDSCPJumpRule(Iptables::Command command,
                                  std::string_view ifname);

  bool ModifyIsolatedGuestDropRule(Iptables::Command command,
                                   std::string_view ifname);

  MinijailedProcessRunner* process_runner_;
  std::unique_ptr<Firewall> firewall_;
  // Owned by PatchpanelDaemon.
  System* system_;

  FRIEND_TEST(DatapathTest, AddVirtualInterfacePair);
  FRIEND_TEST(DatapathTest, ConfigureInterface);
  FRIEND_TEST(DatapathTest, ToggleInterface);

  // A map used for tracking the primary IPv4 dns address associated to a given
  // Shill Device known by its interface name. This is used for redirecting
  // DNS queries of system services when a VPN is connected.
  std::map<std::string, std::string> physical_dns_addresses_;
};

std::ostream& operator<<(std::ostream& stream,
                         const ConnectedNamespace& nsinfo);
std::ostream& operator<<(std::ostream& stream, const DnsRedirectionRule& rule);
std::ostream& operator<<(std::ostream& stream, IpFamily family);

}  // namespace patchpanel

#endif  // PATCHPANEL_DATAPATH_H_
