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

#include <base/containers/flat_set.h>
#include <base/memory/weak_ptr.h>
#include <brillo/any.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/technology.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>

#include "patchpanel/system.h"

// Provided by <shill/dbus-proxies.h>. Use forward declaration here since that
// header is expensive to compile, and thus we should avoid it being spread too
// widely.
namespace org::chromium::flimflam {
class ManagerProxy;
}

namespace patchpanel {

// Listens for shill signals over dbus in order to:
// - Find which network interface are currently managed by shill and to which
//   shill Device they are associated.
// - Figure out which network interface (if any) is being used as the default
//   logical service.
// - Invoke callbacks when the IPConfigs of a shill Device has changed.
class ShillClient {
 public:
  // Represents the properties of an object of org.chromium.flimflam.Device.
  // Only contains the properties we care about.
  struct Device {
    // Interface name of the shill Device, corresponding to the
    // kInterfaceProperty value. b/273741099: The kInterfaceProperty value must
    // be tracked separately to ensure that patchpanel can advertise it in its
    // virtual NetworkDevice messages in the |phys_ifname| field. This allows
    // ARC and dns-proxy to join shill Device information with patchpanel
    // virtual NetworkDevice information without knowing explicitly about
    // Cellular multiplexed interfaces.
    std::string shill_device_interface_property;
    // Technology type of this Device.
    std::optional<net_base::Technology> technology;
    // Interface name of the primary multiplexed interface. Only defined for
    // Cellular Devices. For Cellular Device not using multiplexing, this value
    // is equivalent to the kInterfaceProperty value.
    std::optional<std::string> primary_multiplexed_interface;
    // Index of the network interface used for the packet datapath. This is
    // always derived from the interface name by querying the kernel directly.
    int ifindex;
    // Name of the network interface associated with the shill Device and
    // exposed in DBus as the shill Device kInterfaceProperty value. For a
    // non-Cellular Device this is also the interface used for the packet
    // datapath. For a Cellular Device, this corresponds to the interface
    // associated with the modem.
    std::string ifname;
    // The DBus path of the shill Service currently selected by the shill
    // Device, if any.
    std::string service_path;
    // An anonymous name that uniquely identifies the Service until reboot and
    // does not contain PIIs.
    std::string service_logname;
    // IP configuration for this shill Device. For multiplexed Cellular Devices
    // this corresponds to the IP configuration of the primary network
    // interface.
    net_base::NetworkConfig network_config;
    // The session identifier of the shill Network session this shill Device is
    // associated to.
    std::optional<int> session_id;
    // A string that can be used in logs and will be consistent with shill's
    // Network::LoggingTag() output.
    std::string logging_tag;

    // Return if the device is connected by checking if IPv4 or IPv6 address is
    // available.
    bool IsConnected() const;

    // Return if the device has no IPv4 address and has an IPv6 address.
    bool IsIPv6Only() const;

    // Returns the name of the network interface that is used for the packet
    // datapath. For a non-Cellular Device this is equivalent to |ifname|, and
    // for a Cellular Device this corresponds to the primary multiplexed
    // interface.
    std::string_view ActiveIfname() const;

    // Returns as a string the shill session IDs for the shill Network
    // associated with this shill Device.
    std::string SessionIDString() const;
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

  // The DNS-over-HTTPS service providers which are URLs of the secure DNS
  // service endpoints. Different from the DNSProxyDOHProviders property in
  // shill (see shill/doc/manager-api.doc), this struct does not contain the IPs
  // of name servers since they are not used in patchpanel now.
  using DoHProviders = base::flat_set<std::string>;
  // Client callback for listening to DoH providers change events on the Manager
  // object of shill.
  using DoHProvidersChangeHandler = base::RepeatingCallback<void()>;

  static std::unique_ptr<ShillClient> New(const scoped_refptr<dbus::Bus>& bus,
                                          System* system);

  ShillClient(const ShillClient&) = delete;
  ShillClient& operator=(const ShillClient&) = delete;

  virtual ~ShillClient();

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

  // Registers the provided handler for changes in DoH provider list. The
  // handler will be called once immediately at registration.
  void RegisterDoHProvidersChangedHandler(
      const DoHProvidersChangeHandler& handler);

  void ScanDevices();

  // Updates the cache of NetworkConfig and shill session IDs for the shill
  // Network associated with the interface index |ifindex|.
  void UpdateNetworkConfigCache(int ifindex,
                                const net_base::NetworkConfig& network_config,
                                int session_id);
  void ClearNetworkConfigCache(int ifindex);

  // Finds the shill physical or VPN Device whose "Interface" property matches
  // |shill_device_interface_property|. This function is meant for associating a
  // shill Device to an interface name argument passed directly to patchpanel
  // DBus RPCs for DownstreamNetwork and ConnectNamespace.
  // TODO(b/273744897): Migrate callers to use the future Network primitive
  // directly.
  virtual const Device* GetDeviceByShillDeviceName(
      const std::string& shill_device_interface_property) const;
  // Finds the shill physical or VPN Device whose underlying data interface
  // matches the interface index value |ifindex|. For Devices associated to
  // Cellular multiplexed interfaces, this is the interface index value of the
  // multiplexed interface.
  virtual const Device* GetDeviceByIfindex(int ifindex) const;
  // Returns the cached default logical shill Device, or nullptr if there is no
  // default logical Device defined. Does not initiate a property fetch and does
  // not block.
  virtual const Device* default_logical_device() const;
  // Returns the cached default physical shill Device, or nullptr if there is no
  // default physical Device defined. Does not initiate a property fetch and
  // does not block.
  virtual const Device* default_physical_device() const;
  // Returns interface names of all known shill physical Devices.
  virtual const std::vector<Device> GetDevices() const;
  // Returns the current DoH providers tracked in shill.
  const DoHProviders& doh_providers() const { return doh_providers_; }

  base::WeakPtr<ShillClient> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

 protected:
  ShillClient(const scoped_refptr<dbus::Bus>& bus, System* system);

  // Called by New(). This is isolated from the constructor so that the
  // ShillClient object used in unit test can avoid calling this. Ideally we
  // should have an interface class as the base class to avoid having the real
  // dependencies in the fake class.
  void Initialize();

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
  virtual void OnDeviceNetworkConfigChange(int ifindex);
  void NotifyIPConfigChangeHandlers(const Device& device);
  void NotifyIPv6NetworkChangeHandlers(
      const Device& device, const net_base::NetworkConfig& old_config);

  // Fetches Device dbus properties via dbus for the shill Device identified
  // by |device_path|. Returns std::nullopt if an error occurs. Note that this
  // method will block the current thread.
  virtual std::optional<Device> GetDeviceProperties(
      const dbus::ObjectPath& device_path);

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

  // Gets the ObjectPath of the shill Device that is currently selecting the
  // shill Service |service_path|, or returns std::nullopt if the Service is not
  // active or not selected by any Device. Calling this function will also
  // populate |service_logname_cache_| for |service_path|.
  std::optional<dbus::ObjectPath> GetDevicePathFromServicePath(
      const dbus::ObjectPath& service_path);

  // Getter for FakeShillClient.
  const std::map<int, net_base::NetworkConfig>& network_config_cache() const {
    return network_config_cache_;
  }

  void set_doh_providers_for_testing(const DoHProviders& value);

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

  // Updates |doh_providers_| variable to track the DoH providers from shill.
  // Also invokes the handlers if the list changes.
  void UpdateDoHProviders(const brillo::Any& property_value);

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
  // A map from interface index to ShillClient::IPconfig. This map is updated
  // from the ConfigureNetwork D-Bus calls via UpdateNetworkConfigCache() and
  // ClearNetworkConfigCache. This map tracks the NetworkConfigs on the network
  // interfaces which patchpanel cares about (plus the  the secondary
  // multiplexed APN connection, we call ConfigureNetwork but shill_client does
  // not track it). The NetworkConfig in the Device objects exposed by
  // ShillClient will be updated and retrieved from this cache instead of some
  // other D-Bus calls to shill.
  std::map<int, net_base::NetworkConfig> network_config_cache_;

  // A map of interface index to shill Network session id values. This mapping
  // is updated for a given interface index when UpdateNetworkConfigCache() is
  // called.
  std::map<int, int> session_id_cache_;

  // A map of Service DBus path to Service logging names. This mapping is stable
  // until reboot and only serves to avoid looking Service properties when
  // constructing shill Device objects. Entries from this cache are never
  // removed and it will keep growing overtime in parallel to shill's own list
  // of Services.
  std::map<dbus::ObjectPath, std::string> service_logname_cache_;

  // Tracks the DoH providers from the DNSProxyDOHProviders property on shill's
  // Manager.
  DoHProviders doh_providers_;

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
  // Called when the DNSProxyDOHProviders property changes.
  std::vector<DoHProvidersChangeHandler> doh_provider_handlers_;

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

}  // namespace patchpanel

#endif  // PATCHPANEL_SHILL_CLIENT_H_
