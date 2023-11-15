// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_ARC_SERVICE_H_
#define PATCHPANEL_ARC_SERVICE_H_

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <metrics/metrics_library.h>
#include <net-base/ipv4_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/dbus_client_notifier.h"
#include "patchpanel/forwarding_service.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/subnet.h"

namespace patchpanel {

// Name of the ARC legacy management interface.
constexpr char kArc0Ifname[] = "arc0";
// Name of the veth interface (host facing) used with the ARC legacy management
// interface for ARC container.
constexpr char kVethArc0Ifname[] = "vetharc0";
// Name of the bridge used with the ARC legacy management interface.
constexpr char kArcbr0Ifname[] = "arcbr0";

class ArcService {
 public:
  enum class ArcType {
    kContainer,
    kVM,
  };

  // Helper class for storing some configuration data for an ArcDevice before
  // the ArcDevice is created.
  class ArcConfig {
   public:
    ArcConfig(const MacAddress& mac_addr, std::unique_ptr<Subnet> ipv4_subnet);
    ArcConfig(const ArcConfig&) = delete;
    ArcConfig& operator=(const ArcConfig&) = delete;

    ~ArcConfig() = default;

    MacAddress mac_addr() const { return mac_addr_; }
    void set_mac_addr(const MacAddress& mac) { mac_addr_ = mac; }
    // The static IPv4 subnet CIDR assigned to this ARC device.
    const net_base::IPv4CIDR& ipv4_subnet() const {
      return ipv4_subnet_->base_cidr();
    }
    // The static IPv4 CIDR address assigned to the ARC virtual interface on the
    // host.
    net_base::IPv4CIDR arc_ipv4_address() const {
      return *ipv4_subnet_->CIDRAtOffset(2);
    }
    // The static IPv4 CIDR address assigned to the bridge associated to this
    // device. This corresponds to the next hop for the Android network
    // associated to the ARC virtual interface.
    net_base::IPv4CIDR bridge_ipv4_address() const {
      return *ipv4_subnet_->CIDRAtOffset(1);
    }
    void set_tap_ifname(const std::string& tap) { tap_ = tap; }
    const std::string& tap_ifname() const { return tap_; }

   private:
    // A random MAC address assigned to the ArcDevice for the guest facing
    // interface.
    MacAddress mac_addr_;
    // The static IPv4 subnet allocated for the ArcDevice for the host and guest
    // facing interfaces.
    std::unique_ptr<Subnet> ipv4_subnet_;
    // For ARCVM, the interface name of the TAP device associated the ArcDevice.
    // Always empty for the ARC container.
    std::string tap_;
  };

  // Represents the virtual interface setup both on the host and inside ARC
  // created for every host Network exposed to ARC.
  class ArcDevice {
   public:
    // Technology of the underlying physical device of the virtual device.
    enum class Technology {
      kCellular,
      kEthernet,
      kWiFi,
    };

    ArcDevice(ArcType type,
              std::optional<ArcDevice::Technology> technology,
              std::optional<std::string_view> shill_device_ifname,
              std::string_view arc_device_ifname,
              const MacAddress& arc_device_mac_address,
              const ArcConfig& arc_ipv4_subnet,
              std::string_view bridge_ifname,
              std::string_view guest_device_ifname);
    ~ArcDevice();

    // The type of this ARC device indicating it was created
    ArcType type() const { return type_; }

    // Technology of the underlying physical device of the virtual device, if it
    // exists.
    std::optional<Technology> technology() const { return technology_; }

    // The interface name of the shill Device that this ARC device is bound to.
    // This value is not defined for the "arc0" device used for VPN forwarding.
    // b/273741099: this interface name reflects the kInterfaceProperty value of
    // the shill Device to ensure that patchpanel can advertise it in its
    // virtual NetworkDevice messages in the |phys_ifname| field. This allows
    // ARC and dns-proxy to join shill Device information with patchpanel
    // virtual NetworkDevice information without knowing explicitly about
    // Cellular multiplexed interfaces.
    const std::optional<std::string>& shill_device_ifname() const {
      return shill_device_ifname_;
    }
    // The interface name of the virtual interface created on the host for this
    // ARC device. For the container this corresponds to the half of the veth
    // pair visible on the host. For ARC VM this corresponds to the tap device
    // used by crosvm.
    const std::string& arc_device_ifname() const { return arc_device_ifname_; }
    // MAC address of the virtual interface created on the host for this ARC
    // device.
    const MacAddress& arc_device_mac_address() const {
      return arc_device_mac_address_;
    }
    // The name of the bridge created for this ARC device and to which the
    // virtual interface is attached to on the host.
    const std::string& bridge_ifname() const { return bridge_ifname_; }
    // The interface name of the virtual interface inside the ARC environment.
    // For the container this corresponds to the other half of the veth pair.
    // For ARC VM this corresponds to a virtio interface.
    const std::string& guest_device_ifname() const {
      return guest_device_ifname_;
    }
    // The static IPv4 subnet CIDR assigned to this ARC device.
    const net_base::IPv4CIDR& arc_ipv4_subnet() const {
      return arc_ipv4_subnet_;
    }
    // The static IPv4 CIDR address assigned to the ARC virtual interface.
    const net_base::IPv4CIDR& arc_ipv4_address() const {
      return arc_ipv4_address_;
    }
    // The static IPv4 CIDR address assigned to the bridge associated to this
    // device. This corresponds to the next hop for the Android network
    // associated to the ARC virtual interface.
    const net_base::IPv4CIDR& bridge_ipv4_address() const {
      return bridge_ipv4_address_;
    }
    // Converts this ArcDevice to a patchpanel proto NetworkDevice object
    // passed as a pointer. It is necessary to support externally allocated
    // objects to work well with probotuf repeated embedded message fields.
    void ConvertToProto(NetworkDevice* output) const;

   private:
    ArcType type_;
    std::optional<Technology> technology_;
    std::optional<std::string> shill_device_ifname_;
    std::string arc_device_ifname_;
    MacAddress arc_device_mac_address_;
    net_base::IPv4CIDR arc_ipv4_subnet_;
    net_base::IPv4CIDR arc_ipv4_address_;
    net_base::IPv4CIDR bridge_ipv4_address_;
    std::string bridge_ifname_;
    std::string guest_device_ifname_;
  };

  enum class ArcDeviceEvent {
    kAdded,
    kRemoved,
  };

  using ArcDeviceChangeHandler = base::RepeatingCallback<void(
      const ShillClient::Device&, const ArcDevice&, ArcDeviceEvent)>;

  // Returns for given interface name the host name of a ARC veth pair. Pairs of
  // veth interfaces are only used for the ARC conhtainer.
  static std::string ArcVethHostName(const ShillClient::Device& device);

  // Returns the ARC bridge interface name for the given interface. Software
  // bridges are used both for the ARC container and for ARCVM.
  static std::string ArcBridgeName(const ShillClient::Device& device);

  // All pointers are required, cannot be null, and are owned by the caller.
  ArcService(ArcType arc_type,
             Datapath* datapath,
             AddressManager* addr_mgr,
             ForwardingService* forwarding_service,
             MetricsLibraryInterface* metrics,
             DbusClientNotifier* dbus_client_notifier);
  ArcService(const ArcService&) = delete;
  ArcService& operator=(const ArcService&) = delete;

  ~ArcService();

  bool Start(uint32_t id);
  void Stop(uint32_t id);

  // Returns the IPv4 address of the "arc0" legacy management interface.
  std::optional<net_base::IPv4Address> GetArc0IPv4Address() const;

  // Returns the list of tap device ifnames that patchpanel has created for
  // ARCVM. This list is empty for the ARC container.
  std::vector<std::string> GetTapDevices() const;

  // Returns a list of all patchpanel ARC Devices currently managed by this
  // service and attached to a shill Device.
  std::vector<const ArcDevice*> GetDevices() const;

  // Returns true if the service has been started for ARC container or ARCVM.
  bool IsStarted() const;

  // Build and configure the ARC datapath for the upstream network interface
  // |ifname| managed by Shill.
  void AddDevice(const ShillClient::Device& shill_device);

  // Teardown the ARC datapath associated with the upstream network interface
  // |ifname|.
  void RemoveDevice(const ShillClient::Device& shill_device);

  // Starts the packet datapath on the host for the ARC device |arc_device|.
  // If ARC is running in container mode, the veth interface
  // |arc_device_ifname| is also created together with its counterpart inside
  // the container. Otherwise if ARC is running in VM mode, the tap device must
  // already exist.
  void StartArcDeviceDatapath(const ArcDevice& arc_device);
  // Stops the packet datapath on the host for the ARC device |arc_device|. If
  // ARC is running in container mode, the veth interface |arc_device_ifname| is
  // also destroyed.
  void StopArcDeviceDatapath(const ArcDevice& arc_device);

  // Notifies ArcService that the IP configuration of the physical shill Device
  // |shill_device| changed.
  void UpdateDeviceIPConfig(const ShillClient::Device& shill_device);

  // Start/Stop forwarding multicast traffic to ARC when ARC power state
  // changes.
  // When power state changes into interactive, start forwarding IPv4 and IPv6
  // multicast mDNS and SSDP traffic for all non-WiFi interfaces, and for WiFi
  // interface only when Android WiFi multicast lock is held by any app in ARC.
  // When power state changes into non-interactive, stop forwarding multicast
  // traffic for all interfaces if enabled.
  void NotifyAndroidInteractiveState(bool is_interactive);

  // Start/Stop forwarding WiFi multicast traffic to and from ARC when Android
  // WiFi multicast lock held status changes. Start forwarding IPv4 and IPv6
  // multicast mDNS and SSDP traffic for WiFi interfaces only when
  // device power state is interactive and Android WiFi multicast lock is held
  // by any app in ARC, otherwise stop multicast forwarder for ARC WiFi
  // interface.
  void NotifyAndroidWifiMulticastLockChange(bool is_held);

  // Returns true if MulticastForwarder and BroadcastForwarder are currently
  // running on the ARC WiFi interface. This requires both:
  //  a) ARC is in an interactive state,
  //  b) The Android WiFi multicast lock is held by at least one App.
  bool IsWiFiMulticastForwardingRunning();

 private:
  // Creates ARC interface configuration for the the legacy "arc0" management
  // interface used for VPN forwarding and ADB-over-TCP.
  void AllocateArc0Config();

  // Creates ARC interface configurations for all available IPv4 subnets which
  // will be assigned to ARC Devices as they are added.
  void AllocateAddressConfigs();

  void RefreshMacAddressesInConfigs();

  // Reserve a configuration for an interface.
  std::unique_ptr<ArcConfig> AcquireConfig(ShillClient::Device::Type type);

  // Returns a configuration to the pool.
  void ReleaseConfig(ShillClient::Device::Type type,
                     std::unique_ptr<ArcConfig> config);

  FRIEND_TEST(ArcServiceTest, NotStarted_AddDevice);
  FRIEND_TEST(ArcServiceTest, NotStarted_AddRemoveDevice);
  FRIEND_TEST(ArcServiceTest, VmImpl_ArcvmInterfaceMapping);

  // Type of ARC environment.
  ArcType arc_type_;
  // Routing and iptables controller service, owned by Manager.
  Datapath* datapath_;
  // IPv4 prefix and address manager, owned by Manager.
  AddressManager* addr_mgr_;
  // Service for starting and stopping IPv6 and multicast forwarding between an
  // ArcDevice and its upstream shill Device, owned by Manager.
  ForwardingService* forwarding_service_;
  // UMA metrics client, owned by Manager.
  MetricsLibraryInterface* metrics_;
  // Interface for notifying DBus client about ARC virtual device creation and
  // removal events.
  DbusClientNotifier* dbus_client_notifier_;
  // A set of preallocated ARC interface configurations keyed by technology type
  // and used for setting up ARCVM TAP devices at VM booting time.
  std::map<ShillClient::Device::Type, std::deque<std::unique_ptr<ArcConfig>>>
      available_configs_;
  // The list of all ARC static IPv4 and interface configurations. Also includes
  // the ARC management interface arc0 for ARCVM.
  std::vector<ArcConfig*> all_configs_;
  // All ARC static IPv4 and interface configurations currently assigned to
  // active ARC devices stored in |devices_|.
  std::map<std::string, std::unique_ptr<ArcConfig>> assigned_configs_;
  // The ARC Devices corresponding to the host upstream network interfaces,
  // keyed by upstream interface name.
  std::map<std::string, ArcDevice> devices_;
  // ARCVM hardcodes its interface name as eth%d (starting from 0). This is a
  // mapping of its TAP interface name to the interface name inside ARCVM.
  std::map<std::string, std::string> arcvm_guest_ifnames_;
  // The static IPv4 configuration for the "arc0" management ArcDevice. This
  // configuration is always assigned if ARC is running.
  std::unique_ptr<ArcConfig> arc0_config_;
  // The "arc0" management ArcDevice associated with the virtual interface arc0
  // used for legacy adb-over-tcp support and VPN forwarding. This ARC device is
  // not associated with any given shill physical Device and is always created.
  // This ARC device is defined iff ARC is running.
  std::optional<ArcDevice> arc0_device_;
  // The PID of the ARC container instance or the CID of ARCVM instance.
  uint32_t id_;
  // All shill Devices currently managed by shill, keyed by shill Device name.
  std::map<std::string, ShillClient::Device> shill_devices_;
  // Whether multicast lock is held by any app in ARC, used to decide whether
  // to start/stop forwarding multicast traffic to ARC on WiFi.
  bool is_android_wifi_multicast_lock_held_ = false;
  // Whether device is interactive, used to decide whether to start/stop
  // forwarding multicast traffic to ARC on all multicast enabled networks.
  bool is_arc_interactive_ = true;

  base::WeakPtrFactory<ArcService> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream,
                         const ArcService::ArcDevice& arc_device);
std::ostream& operator<<(std::ostream& stream, ArcService::ArcType arc_type);

}  // namespace patchpanel

#endif  // PATCHPANEL_ARC_SERVICE_H_
