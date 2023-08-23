// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SHILL_CLIENT_H_
#define PATCHPANEL_SHILL_CLIENT_H_

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <dbus/object_path.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <shill/dbus-proxies.h>

#include "patchpanel/system.h"

namespace patchpanel {

// Listens for shill signals over dbus in order to:
// - Find which network interface are currently managed by shill and to which
//   shill Device they are associated.
// - Figure out which network interface (if any) is being used as the default
//   logical service.
// - Invoke callbacks when the IPConfigs of a shill Device has changed.
class ShillClient {
 public:
  // IPConfig for a shill Device. If the shill Device does not have a valid
  // ipv4/ipv6 config, the corresponding fields will be empty or std::nullopt.
  // TODO(jiejiang): add the following fields into this struct:
  // - IPv4 search domains
  // - IPv6 search domains
  // - MTU (one only per network)
  struct IPConfig {
    std::optional<net_base::IPv4CIDR> ipv4_cidr;
    std::optional<net_base::IPv4Address> ipv4_gateway;
    std::vector<std::string> ipv4_dns_addresses;

    // Note due to the limitation of shill, we will only get one IPv6 address
    // from it. This address should be the privacy address for device with type
    // of ethernet or wifi.
    std::optional<net_base::IPv6CIDR> ipv6_cidr;
    std::optional<net_base::IPv6Address> ipv6_gateway;
    std::vector<std::string> ipv6_dns_addresses;
    bool operator==(const IPConfig& b) const {
      return ipv4_cidr == b.ipv4_cidr && ipv4_gateway == b.ipv4_gateway &&
             std::set<std::string>(ipv4_dns_addresses.begin(),
                                   ipv4_dns_addresses.end()) ==
                 std::set<std::string>(b.ipv4_dns_addresses.begin(),
                                       b.ipv4_dns_addresses.end()) &&
             ipv6_cidr == b.ipv6_cidr && ipv6_gateway == b.ipv6_gateway &&
             std::set<std::string>(ipv6_dns_addresses.begin(),
                                   ipv6_dns_addresses.end()) ==
                 std::set<std::string>(b.ipv6_dns_addresses.begin(),
                                       b.ipv6_dns_addresses.end());
    }
  };

  // Represents the properties of an object of org.chromium.flimflam.Device.
  // Only contains the properties we care about.
  // TODO(jiejiang): add the following fields into this struct:
  // - the connection state of the Service, if possible by translating back to
  //   the enum shill::Service::ConnectState
  struct Device {
    // A subset of shill::Technology::Type.
    enum class Type {
      kUnknown,
      kCellular,
      kEthernet,
      kEthernetEap,
      kGuestInterface,
      kLoopback,
      kPPP,
      kTunnel,
      kVPN,
      kWifi,
    };

    // Interface name of the shill Device, corresponding to the
    // kInterfaceProperty value. b/273741099: The kInterfaceProperty value must
    // be tracked separately to ensure that patchpanel can advertise it in its
    // virtual NetworkDevice messages in the |phys_ifname| field. This allows
    // ARC and dns-proxy to join shill Device information with patchpanel
    // virtual NetworkDevice information without knowing explicitly about
    // Cellular multiplexed interfaces.
    std::string shill_device_interface_property;
    // Technology type of this Device.
    Type type;
    // Interface name of the primary multiplexed interface. Only defined for
    // Cellular Devices. For Cellular Device not using multiplexing, this value
    // is equivalent to the kInterfaceProperty value.
    std::optional<std::string> primary_multiplexed_interface;
    // Index of the network interface used for the packet datapath. This is
    // always derived from the interface name by querying the kernel directly.
    int ifindex;
    // Name of the network interface used for the packet datapath. This
    // currently corresponds to the shill Device kInterfaceProperty value.
    std::string ifname;
    // The DBus path of the shill Service currently selected by the shill
    // Device, if any.
    std::string service_path;
    // IP configuration for this shill Device. For multiplexed Cellular Devices
    // this corresponds to the IP configuration of the primary network
    // interface.
    IPConfig ipconfig;

    // Return if the device is connected by checking if IPv4 or IPv6 address is
    // available.
    bool IsConnected() const;

    // Return if the device has no IPv4 address and has an IPv6 address.
    bool IsIPv6Only() const;
  };

  // Client callback for learning when shill default logical and physical
  // network change. |new_device| can be null if there is no logical or physical
  // network currently. |prev_device| can be null if there was no logical or
  // physical network before.
  using DefaultDeviceChangeHandler = base::RepeatingCallback<void(
      const Device* new_device, const Device* prev_device)>;
  // Client callback for learning which shill Devices were created or removed by
  // shill.
  using DevicesChangeHandler = base::RepeatingCallback<void(
      const std::vector<Device>& added, const std::vector<Device>& removed)>;
  // Client callback for listening to IPConfig changes on any shill Device with
  // interface name |ifname|. Changes to the IP configuration of a VPN
  // connection are not taken into account.
  using IPConfigsChangeHandler =
      base::RepeatingCallback<void(const Device& device)>;

  // Client callback for listening to IPv6 network changes on any shill physical
  // Device. The changes are identified by IPv6 prefix change. Note that any
  // IPv6 prefix change also triggers all IPConfigsChangeHandler registered
  // callbacks. Changes to the IPv6 network of a VPN connection are not taken
  // into account.
  using IPv6NetworkChangeHandler =
      base::RepeatingCallback<void(const Device& device)>;

  explicit ShillClient(const scoped_refptr<dbus::Bus>& bus, System* system);
  ShillClient(const ShillClient&) = delete;
  ShillClient& operator=(const ShillClient&) = delete;

  virtual ~ShillClient() = default;

  // Registers the provided handler for changes in shill default logical or
  // physical network.
  // The handler will be called once immediately at registration
  // with the current default logical or physical network as |new_device| and
  // an empty Device as |prev_device|.
  void RegisterDefaultLogicalDeviceChangedHandler(
      const DefaultDeviceChangeHandler& handler);
  void RegisterDefaultPhysicalDeviceChangedHandler(
      const DefaultDeviceChangeHandler& handler);

  void RegisterDevicesChangedHandler(const DevicesChangeHandler& handler);

  void RegisterIPConfigsChangedHandler(const IPConfigsChangeHandler& handler);

  void RegisterIPv6NetworkChangedHandler(
      const IPv6NetworkChangeHandler& handler);

  void ScanDevices();

  // Finds the shill physical or VPN Device whose "Interface" property matches
  // |shill_device_interface_property|. This function is meant for associating a
  // shill Device to an interface name argument passed directly to patchpanel
  // DBus RPCs for DownstreamNetwork and ConnectNamespace.
  // TODO(b/273744897): Migrate callers to use the future Network primitive
  // directly.
  virtual const Device* GetDevice(
      const std::string& shill_device_interface_property) const;
  // Returns the cached default logical shill Device, or nullptr if there is no
  // default logical Device defined. Does not initiate a property fetch and does
  // not block.
  virtual const Device* default_logical_device() const;
  // Returns the cached default physical shill Device, or nullptr if there is no
  // default physical Device defined. Does not initiate a property fetch and
  // does not block.
  virtual const Device* default_physical_device() const;
  // Returns interface names of all known shill physical Devices.
  const std::vector<Device> GetDevices() const;

 protected:
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  void OnDevicePropertyChangeRegistration(
      const std::string& dbus_interface_name,
      const std::string& signal_name,
      bool success);
  void OnDevicePropertyChange(const dbus::ObjectPath& device_path,
                              const std::string& property_name,
                              const brillo::Any& property_value);
  void OnDevicePrimaryMultiplexedInterfaceChange(
      const dbus::ObjectPath& device_path,
      const std::string& primary_multiplexed_interface);
  void OnDeviceIPConfigChange(const dbus::ObjectPath& device_path);
  void NotifyIPConfigChangeHandlers(const Device& device);
  void NotifyIPv6NetworkChangeHandlers(
      const Device& device, const std::optional<net_base::IPv6CIDR>& old_cidr);

  // Fetches Device dbus properties via dbus for the shill Device identified
  // by |device_path|. Returns false if an error occurs. Note that this method
  // will block the current thread.
  virtual bool GetDeviceProperties(const dbus::ObjectPath& device_path,
                                   Device* output);

  // Updates the current default logical and physical shill Devices for the
  // system, and notifies listeners if there was any change.
  void UpdateDefaultDevices();

  // Returns the DBus paths of all shill Services. Can be overridden for
  // testing.
  virtual std::vector<dbus::ObjectPath> GetServices();

  // Fetches shill Device DBus properties of the shill Device which has selected
  // the shill Service with DBus path |service_path|. Returns std::nullopt if an
  // error occurs or if the Service is not currently active. Note that this
  // method will block the current thread. Can be overridden for testing.
  virtual std::optional<Device> GetDeviceFromServicePath(
      const dbus::ObjectPath& service_path);

 private:
  // Updates the list of currently known shill Devices, adding or removing
  // Device tracking entries accordingly. Listeners that have registered a
  // DevicesChangeHandler callback gets notified about any new or old Device
  // change.
  void UpdateDevices(const brillo::Any& property_value);

  // Sets the internal shill Device variable tracking the system default logical
  // network. Calls the registered client handlers if the default logical
  // network changed. If a VPN is connected, the logical Device pertains to the
  // VPN connection.
  void SetDefaultLogicalDevice(const std::optional<Device>& device);

  // Sets the internal shill Device variable tracking the system default
  // physical network. Calls the registered client handlers if the default
  // physical network changed.
  void SetDefaultPhysicalDevice(const std::optional<Device>& device);

  // Parses the |ipconfig_properties| as the IPConfigs property of the shill
  // Device identified by |device_path|, which should be a list of object paths
  // of IPConfigs.
  IPConfig ParseIPConfigsProperty(const dbus::ObjectPath& device_path,
                                  const brillo::Any& ipconfig_paths);

  // Tracks the system default physical network chosen by shill.
  std::optional<Device> default_physical_device_;
  // Tracks the system default logical network chosen by shill. This corresponds
  // to the physical or VPN shill Device associated with the default logical
  // network service.
  std::optional<Device> default_logical_device_;
  // Maps of all current shill physical Devices that are active, indexed by
  // shill Device identifier. VPN Devices and inactive Devices are ignored.
  std::map<dbus::ObjectPath, Device> devices_;
  // Sets of shill Device Dbus object path for all the shill physical Devices
  // seen so far. Unlike |devices_|, entries in this set will never be
  // removed during the lifetime of this class. We maintain this set mainly for
  // keeping track of the shill Device object proxies we have created, to avoid
  // registering the handler on the same object twice.
  std::set<dbus::ObjectPath> known_device_paths_;
  // A map used for remembering the interface name and interface index of a
  // shill Device after the underlying network interface has been removed, keyed
  // by the shill Device's "Interface" property. This information is necessary
  // when cleaning up the state of various subsystems in patchpanel that
  // directly references the interface name or the interface index. This
  // information can be missing when:
  //   - After receiving the interface removal event (RTM_DELLINK event or shill
  //   DBus event), the interface index cannot be retrieved anymore.
  //   - b/273741099: After the disconnection of the primary Network of a
  //   Cellular Device, the name of primary multiplexed interface is unknown.
  std::map<std::string, std::pair<std::string, int>> datapath_interface_cache_;

  // Called when the shill Device used as the default logical network changes.
  std::vector<DefaultDeviceChangeHandler> default_logical_device_handlers_;
  // Called when the shill Device used as the default physical network changes.
  std::vector<DefaultDeviceChangeHandler> default_physical_device_handlers_;
  // Called when the list of network interfaces managed by shill changes.
  std::vector<DevicesChangeHandler> device_handlers_;
  // Called when the IPConfigs of any shill Device changes.
  std::vector<IPConfigsChangeHandler> ipconfigs_handlers_;
  // Called when the IPv6 network of any shill Device changes.
  std::vector<IPv6NetworkChangeHandler> ipv6_network_handlers_;

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxy> manager_proxy_;
  // Owned by Manager
  System* system_;

  base::WeakPtrFactory<ShillClient> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, const ShillClient::Device& dev);
std::ostream& operator<<(std::ostream& stream,
                         const std::optional<ShillClient::Device>& dev);
std::ostream& operator<<(std::ostream& stream, const ShillClient::Device* dev);
std::ostream& operator<<(std::ostream& stream,
                         const ShillClient::Device::Type type);
std::ostream& operator<<(std::ostream& stream,
                         const ShillClient::IPConfig& ipconfig);

}  // namespace patchpanel

#endif  // PATCHPANEL_SHILL_CLIENT_H_
