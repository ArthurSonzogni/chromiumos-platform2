// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DBUS_CLIENT_H_
#define PATCHPANEL_DBUS_CLIENT_H_

#include <initializer_list>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <brillo/brillo_export.h>
// Ignore Wconversion warnings in dbus headers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#pragma GCC diagnostic pop
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/network_config.h>
#include <net-base/network_priority.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

namespace org::chromium {
class PatchPanelProxyInterface;
}  // namespace org::chromium

namespace patchpanel {

// Simple wrapper around patchpanel DBus API. All public functions are blocking
// DBus calls to patchpaneld (asynchronous calls are mentioned explicitly). The
// method names and protobuf schema used by patchpanel DBus API are defined in
// platform2/system_api/dbus/patchpanel. Types and classes generated from the
// patchpanel protobuf schema are not directly used and are instead wrapped with
// lightweight structs and enums defined in this file. Access control for
// clients is defined // in platform2/patchpanel/dbus.
class BRILLO_EXPORT Client {
 public:
  // See TrafficCounter.IpFamily in patchpanel_service.proto.
  enum class IPFamily {
    kIPv4,
    kIPv6,
  };

  // See TrafficCounter.Source in patchpanel_service.proto.
  enum class TrafficSource {
    kUnknown,
    kChrome,
    kUser,
    kUpdateEngine,
    kSystem,
    kVpn,
    kArc,
    kBorealisVM,
    kBruschettaVM,
    kCrostiniVM,
    kParallelsVM,
    kTethering,
    kWiFiDirect,
    kWiFiLOHS,
  };

  static constexpr std::initializer_list<TrafficSource> kAllTrafficSources = {
      TrafficSource::kUnknown,      TrafficSource::kChrome,
      TrafficSource::kUser,         TrafficSource::kUpdateEngine,
      TrafficSource::kSystem,       TrafficSource::kVpn,
      TrafficSource::kArc,          TrafficSource::kBorealisVM,
      TrafficSource::kBruschettaVM, TrafficSource::kCrostiniVM,
      TrafficSource::kParallelsVM,  TrafficSource::kTethering,
      TrafficSource::kWiFiDirect,   TrafficSource::kWiFiLOHS};

  struct TrafficVector {
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;

    bool operator==(const TrafficVector&) const;
    TrafficVector& operator+=(const TrafficVector&);
    TrafficVector& operator-=(const TrafficVector&);
    TrafficVector operator+(const TrafficVector&) const;
    TrafficVector operator-(const TrafficVector&) const;
    TrafficVector operator-() const;
  };

  static constexpr TrafficVector kZeroTraffic = {
      .rx_bytes = 0,
      .tx_bytes = 0,
      .rx_packets = 0,
      .tx_packets = 0,
  };

  struct TrafficCounter {
    TrafficSource source;
    std::string ifname;
    IPFamily ip_family;
    TrafficVector traffic;
  };

  // See NetworkDevice.GuestType in patchpanel_service.proto.
  enum class GuestType {
    kArcContainer,
    kArcVm,
    kTerminaVm,
    kParallelsVm,
  };

  // See NetworkDevice.TechnologyType in patchpanel_service.proto.
  enum class TechnologyType {
    kCellular,
    kEthernet,
    kWiFi,
  };

  // See NetworkDeviceChangedSignal in patchpanel_service.proto.
  enum class VirtualDeviceEvent {
    kAdded,
    kRemoved,
  };

  // See NetworkDevice in patchpanel_service.proto.
  struct VirtualDevice {
    std::string ifname;
    std::string phys_ifname;
    std::string guest_ifname;
    net_base::IPv4Address ipv4_addr;
    net_base::IPv4Address host_ipv4_addr;
    std::optional<net_base::IPv4CIDR> ipv4_subnet;
    GuestType guest_type;
    std::optional<net_base::IPv4Address> dns_proxy_ipv4_addr;
    std::optional<net_base::IPv6Address> dns_proxy_ipv6_addr;
    std::optional<TechnologyType> technology_type;
  };

  // See ConnectNamespaceResponse in patchpanel_service.proto.
  struct ConnectedNamespace {
    net_base::IPv4CIDR ipv4_subnet;
    std::string peer_ifname;
    net_base::IPv4Address peer_ipv4_address;
    std::string host_ifname;
    net_base::IPv4Address host_ipv4_address;
    std::string netns_name;
  };

  // See DownstreamNetwork in patchpanel_service.proto.
  struct DownstreamNetwork {
    std::string ifname;
    net_base::IPv4CIDR ipv4_subnet;
    net_base::IPv4Address ipv4_gateway_addr;
  };

  // See NetworkClientInfo in patchpanel_service.proto.
  struct NetworkClientInfo {
    std::vector<uint8_t> mac_addr;
    net_base::IPv4Address ipv4_addr;
    std::vector<net_base::IPv6Address> ipv6_addresses;
    std::string hostname;
    std::string vendor_class;
  };

  // See ModifyPortRuleRequest.Operation in patchpanel_service.proto.
  enum class FirewallRequestOperation {
    kCreate,
    kDelete,
  };

  // See ModifyPortRuleRequest.RuleType in patchpanel_service.proto.
  enum class FirewallRequestType {
    kAccess,
    kLockdown,
    kForwarding,
  };

  // See ModifyPortRuleRequest.Protocol in patchpanel_service.proto.
  enum class FirewallRequestProtocol {
    kTcp,
    kUdp,
  };

  // See SetDnsRedirectionRuleRequest in patchpanel_service.proto.
  enum class DnsRedirectionRequestType {
    kDefault,
    kArc,
    kUser,
    kExcludeDestination,
  };

  // See NeighborReachabilityEventSignal.Role in patchpanel_service.proto.
  enum class NeighborRole {
    kGateway,
    kDnsServer,
    kGatewayAndDnsServer,
  };

  // See NeighborReachabilityEventSignal.EventType in patchpanel_service.proto.
  enum class NeighborStatus {
    kFailed,
    kReachable,
  };

  // See SetFeatureFlagRequest.FeatureFlag in patchpanel_service.proto.
  enum class FeatureFlag {
    kWiFiQoS,
    kClat,
  };

  // See NeighborReachabilityEventSignal in patchpanel_service.proto.
  struct NeighborReachabilityEvent {
    int ifindex;
    std::string ip_addr;
    NeighborRole role;
    NeighborStatus status;
  };

  // Contains the options for creating the IPv4 DHCP server.
  // TODO(b/239559602) Fill out DHCP options:
  //  - If the upstream network has a DHCP lease, copy relevant options.
  //  - Forward DHCP WPAD proxy configuration if advertised by the upstream
  //    network.
  struct DHCPOptions {
    std::vector<net_base::IPv4Address> dns_server_addresses;
    std::vector<std::string> domain_search_list;
    bool is_android_metered = false;
  };

  // b/294287313: Helper struct to notify patchpanel about the IPv6
  // configuration of an uplink network that patchpanel cannot track with a
  // shill Device. This is necessary when a secondary multiplexed PDN connection
  // is used for tethering.
  struct UplinkIPv6Configuration {
    net_base::IPv6CIDR uplink_address;
    std::vector<net_base::IPv6Address> dns_server_addresses;
  };

  // Contains the network IPv4 subnets assigned to a Termina VM and to its inner
  // LXD container, and the name of the tap device created by patchpanel for the
  // VM. See TerminaVmStartupResponse in patchpanel_service.proto.
  struct TerminaAllocation {
    // Tap device interface name created for the VM.
    std::string tap_device_ifname;
    // The /30 IPv4 subnet assigned to the VM.
    net_base::IPv4CIDR termina_ipv4_subnet;
    // The IPv4 address assigned to the VM, contained inside |ipv4_subnet|.
    net_base::IPv4Address termina_ipv4_address;
    // The next hop IPv4 address for the VM, contained inside |ipv4_subnet|.
    net_base::IPv4Address gateway_ipv4_address;
    // The /28 container IPv4 subnet assigned to the inner LXD container.
    net_base::IPv4CIDR container_ipv4_subnet;
    // The IPv4 address the inner LXD container, contained inside
    // |container_ipv4_subnet|.
    net_base::IPv4Address container_ipv4_address;
  };

  // Contains the network IPv4 subnet assigned to a Parallels VM and the name
  // of the tap device created by patchpanel for the VM. See
  // ParallelsVmStartupResponse in patchpanel_service.proto.
  struct ParallelsAllocation {
    // Tap device interface name created for the VM.
    std::string tap_device_ifname;
    // The /30 IPv4 subnet assigned to the VM.
    net_base::IPv4CIDR parallels_ipv4_subnet;
    // The IPv4 address assigned to the VM, contained inside |ipv4_subnet|.
    net_base::IPv4Address parallels_ipv4_address;
  };

  // Contains the network IPv4 subnet assigned to a Bruschetta VM and the name
  // of the tap device created by patchpanel for the VM. See
  // BruschettaVmStartupResponse in patchpanel_service.proto.
  struct BruschettaAllocation {
    // Tap device interface name created for the VM.
    std::string tap_device_ifname;
    // The /30 IPv4 subnet assigned to the VM.
    net_base::IPv4CIDR bruschetta_ipv4_subnet;
    // The IPv4 address assigned to the VM, contained inside |ipv4_subnet|.
    net_base::IPv4Address bruschetta_ipv4_address;
    // The next hop IPv4 address for the VM, contained inside |ipv4_subnet|.
    net_base::IPv4Address gateway_ipv4_address;
  };

  // Contains the network IPv4 subnet assigned to a Borealis VM and the name
  // of the tap device created by patchpanel for the VM. See
  // BorealisVmStartupResponse in patchpanel_service.proto.
  struct BorealisAllocation {
    // Tap device interface name created for the VM.
    std::string tap_device_ifname;
    // The /30 IPv4 subnet assigned to the VM.
    net_base::IPv4CIDR borealis_ipv4_subnet;
    // The IPv4 address assigned to the VM, contained inside |ipv4_subnet|.
    net_base::IPv4Address borealis_ipv4_address;
    // The next hop IPv4 address for the VM, contained inside |ipv4_subnet|.
    net_base::IPv4Address gateway_ipv4_address;
  };

  // Contains the list of tap devices initially created by patchpanel as well as
  // the IPv4 address of the "arc0" legacy management interface.
  struct ArcVMAllocation {
    net_base::IPv4Address arc0_ipv4_address;
    std::vector<std::string> tap_device_ifnames;
  };

  // See NetworkTechnology in patchpanel_service.proto.
  enum class NetworkTechnology {
    kCellular,
    kEthernet,
    kVPN,
    kWiFi,
  };

  using GetTrafficCountersCallback =
      base::OnceCallback<void(const std::vector<TrafficCounter>&)>;
  using NeighborReachabilityEventHandler =
      base::RepeatingCallback<void(const NeighborReachabilityEvent&)>;
  using VirtualDeviceEventHandler =
      base::RepeatingCallback<void(VirtualDeviceEvent, const VirtualDevice&)>;
  using CreateTetheredNetworkCallback =
      base::OnceCallback<void(base::ScopedFD)>;
  using CreateLocalOnlyNetworkCallback =
      base::OnceCallback<void(base::ScopedFD)>;
  using GetDownstreamNetworkInfoCallback =
      base::OnceCallback<void(bool success,
                              const DownstreamNetwork& downstream_network,
                              const std::vector<NetworkClientInfo>& clients)>;
  using ConfigureNetworkCallback = base::OnceCallback<void(bool success)>;

  // Creates the instance with the system dbus object which is created
  // internally. The dbus object will shutdown at destruction.
  static std::unique_ptr<Client> New();

  // Creates the instance with the dbus object.
  static std::unique_ptr<Client> New(const scoped_refptr<dbus::Bus>& bus);

  // Creates the instance by injecting the bus and proxy objects.
  static std::unique_ptr<Client> NewForTesting(
      scoped_refptr<dbus::Bus> bus,
      std::unique_ptr<org::chromium::PatchPanelProxyInterface> proxy);

  static bool IsArcGuest(GuestType guest_type);
  static std::string TrafficSourceName(TrafficSource source);
  static std::string ProtocolName(FirewallRequestProtocol protocol);
  static std::string NeighborRoleName(NeighborRole role);
  static std::string NeighborStatusName(NeighborStatus status);

  virtual ~Client() = default;

  virtual void RegisterOnAvailableCallback(
      base::RepeatingCallback<void(bool)> callback) = 0;

  // |callback| will be invoked if patchpanel exits and/or the DBus service
  // owner changes. The parameter will be false if the process is gone (no
  // owner) or true otherwise.
  virtual void RegisterProcessChangedCallback(
      base::RepeatingCallback<void(bool)> callback) = 0;

  virtual bool NotifyArcStartup(pid_t pid) = 0;
  virtual bool NotifyArcShutdown() = 0;

  virtual std::optional<ArcVMAllocation> NotifyArcVmStartup(uint32_t cid) = 0;
  virtual bool NotifyArcVmShutdown(uint32_t cid) = 0;

  virtual std::optional<TerminaAllocation> NotifyTerminaVmStartup(
      uint32_t cid) = 0;
  virtual bool NotifyTerminaVmShutdown(uint32_t cid) = 0;

  virtual std::optional<ParallelsAllocation> NotifyParallelsVmStartup(
      uint64_t vm_id, int subnet_index) = 0;
  virtual bool NotifyParallelsVmShutdown(uint64_t vm_id) = 0;

  virtual std::optional<BruschettaAllocation> NotifyBruschettaVmStartup(
      uint64_t vm_id) = 0;
  virtual bool NotifyBruschettaVmShutdown(uint64_t vm_id) = 0;

  virtual std::optional<BorealisAllocation> NotifyBorealisVmStartup(
      uint32_t vm_id) = 0;
  virtual bool NotifyBorealisVmShutdown(uint32_t vm_id) = 0;

  // Reset the VPN routing intent mark on a socket to the default policy for
  // the current uid. This is in general incorrect to call this method for
  // a socket that is already connected.
  virtual bool DefaultVpnRouting(const base::ScopedFD& socket) = 0;

  // Mark a socket to be always routed through a VPN if there is one.
  // Must be called before the socket is connected.
  virtual bool RouteOnVpn(const base::ScopedFD& socket) = 0;

  // Mark a socket to be always routed through the physical network.
  // Must be called before the socket is connected.
  virtual bool BypassVpn(const base::ScopedFD& socket) = 0;

  // Sends a ConnectNamespaceRequest for the given namespace pid. Returns a
  // pair with a valid ScopedFD and the ConnectedNamespace response
  // received if the request succeeded. Closing the ScopedFD will teardown the
  // veth and routing setup and free the allocated IPv4 subnet.
  // If |outbound_ifname| is defined, it must be the kInterfaceProperty value of
  // a shill Device. If the shill Device uses interface multiplexing (Cellular),
  // ConnectNamespace is implicitly configured with the primary multiplexed
  // interface.
  // TODO(b/273744897): Migrate ConnectNamespace to use a patchpanel Network id.
  virtual std::pair<base::ScopedFD, ConnectedNamespace> ConnectNamespace(
      pid_t pid,
      const std::string& outbound_ifname,
      bool forward_user_traffic,
      bool route_on_vpn,
      TrafficSource traffic_source,
      bool static_ipv6 = false) = 0;

  // Gets the traffic counters kept by patchpanel asynchronously, |callback|
  // will be called with the counters once they are ready, or with an empty
  // vector when an error happen. |devices| is the set of interfaces (shill
  // devices) for which counters should be returned, any unknown interfaces will
  // be ignored. If |devices| is empty, counters for all known interfaces will
  // be returned.
  virtual void GetTrafficCounters(const std::set<std::string>& devices,
                                  GetTrafficCountersCallback callback) = 0;

  // Sends a ModifyPortRuleRequest to modify iptables ingress rules.
  // This should only be called by permission_broker's 'devbroker'.
  virtual bool ModifyPortRule(FirewallRequestOperation op,
                              FirewallRequestType type,
                              FirewallRequestProtocol proto,
                              const std::string& input_ifname,
                              const std::string& input_dst_ip,
                              uint32_t input_dst_port,
                              const std::string& dst_ip,
                              uint32_t dst_port) = 0;

  // Start or stop VPN lockdown. When VPN lockdown is enabled and no VPN
  // connection exists, any non-ARC traffic that would be routed to a VPN
  // connection is instead rejected. ARC traffic is ignored because Android
  // already implements VPN lockdown. This method is async. It will return
  // directly instead of waiting for the result of the D-Bus request.
  virtual void SetVpnLockdown(bool enable) = 0;

  // Sends a SetDnsRedirectionRuleRequest to modify iptables rules for DNS
  // proxy. This should only be called by 'dns-proxy'. If successful, it returns
  // a ScopedFD. The rules lifetime is tied to the file descriptor. This returns
  // an invalid file descriptor upon failure.
  // |input_ifname| must be the kInterfaceProperty value of a shill Device.
  // If the shill Device uses interface multiplexing (Cellular), RedirectDns is
  // implicitly configured with the primary multiplexed interface.
  // TODO(b/273744897): Migrate ConnectNamespace to use a patchpanel Network id.
  virtual base::ScopedFD RedirectDns(
      DnsRedirectionRequestType type,
      const std::string& input_ifname,
      const std::string& proxy_address,
      const std::vector<std::string>& nameservers,
      const std::string& host_ifname) = 0;

  // Obtains a list of NetworkDevices currently managed by patchpanel.
  virtual std::vector<VirtualDevice> GetDevices() = 0;

  // Registers a handler that will be called upon receiving a signal indicating
  // that a network device managed by patchpanel was added or removed.
  virtual void RegisterVirtualDeviceEventHandler(
      VirtualDeviceEventHandler handler) = 0;

  // Registers a handler that will be called on receiving a neighbor
  // reachability event. Currently these events are generated only for WiFi
  // devices. The handler is registered for as long as this patchpanel::Client
  // instance is alive.
  virtual void RegisterNeighborReachabilityEventHandler(
      NeighborReachabilityEventHandler handler) = 0;

  // Sends request for creating an L3 network on |downstream_ifname|, sharing
  // the Internet connection of |upstream_ifname| with the created network.
  // Returns true if the request was successfully sent, false otherwise. After
  // the request completes successfully, |callback| is ran with a file
  // descriptor controlling the lifetime of the tethering setup. The tethering
  // setup is torn down when the file descriptor is closed by the client. If the
  // request failed, |callback| is ran with an invalid ScopedFD value.
  // The additional argument |uplink_ipv6_config| should only be used to specify
  // the IPv6 configuration of a secondary PDN used as the uplink network where
  // patchpanel is not able to track the uplink network directly.
  virtual bool CreateTetheredNetwork(
      const std::string& downstream_ifname,
      const std::string& upstream_ifname,
      const std::optional<DHCPOptions>& dhcp_options,
      const std::optional<UplinkIPv6Configuration>& uplink_ipv6_config,
      const std::optional<int>& mtu,
      CreateTetheredNetworkCallback callback) = 0;

  // Sends request for creating a local-only L3 network on |ifname|.
  // Returns true if the request was successfully sent, false otherwise. After
  // the request completes successfully, |callback| is ran with a file
  // descriptor controlling the lifetime of the local only network setup. The
  // local only network setup is torn down when the file descriptor is closed by
  // the client. If the request failed, |callback| is ran with an invalid
  // ScopedFD value.
  virtual bool CreateLocalOnlyNetwork(
      const std::string& ifname, CreateLocalOnlyNetworkCallback callback) = 0;

  // Gets L3 information about a downstream network created with
  // CreateTetheredNetwork or CreateLocalOnlyNetwork on |ifname|
  // and all its connected clients. Returns true if the request was successfully
  // sent, false otherwise. |callback| is ran after the request has completed.
  virtual bool GetDownstreamNetworkInfo(
      const std::string& ifname, GetDownstreamNetworkInfoCallback callback) = 0;

  // Sends request for setting the feature flag of |flag| to |enable|.
  // Returns true if the request was successfully sent, false otherwise.
  virtual bool SendSetFeatureFlagRequest(FeatureFlag flag, bool enable) = 0;

  // Sends a request to configure an IP network or modify the configuration of
  // an existing IP network on a certain physical or VPN network interface.
  virtual bool ConfigureNetwork(int interface_index,
                                std::string_view interface_name,
                                uint32_t area,
                                const net_base::NetworkConfig& network_config,
                                net_base::NetworkPriority priority,
                                NetworkTechnology technology,
                                ConfigureNetworkCallback callback) = 0;

 protected:
  Client() = default;
};

BRILLO_EXPORT std::ostream& operator<<(
    std::ostream& stream, const Client::NeighborReachabilityEvent& event);

BRILLO_EXPORT std::ostream& operator<<(
    std::ostream& stream, const Client::NetworkTechnology& technology);

BRILLO_EXPORT void SerializeNetworkConfig(const net_base::NetworkConfig& in,
                                          patchpanel::NetworkConfig* out);

}  // namespace patchpanel

#endif  // PATCHPANEL_DBUS_CLIENT_H_
