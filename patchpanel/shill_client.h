// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SHILL_CLIENT_H_
#define PATCHPANEL_SHILL_CLIENT_H_

#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <shill/dbus-proxies.h>

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
  // ipv4/ipv6 config, the corresponding fields will be empty or 0.
  // TODO(jiejiang): add the following fields into this struct:
  // - IPv4 search domains
  // - IPv6 search domains
  // - MTU (one only per network)
  struct IPConfig {
    int ipv4_prefix_length;
    std::string ipv4_address;
    std::string ipv4_gateway;
    std::vector<std::string> ipv4_dns_addresses;

    int ipv6_prefix_length;
    // Note due to the limitation of shill, we will only get one IPv6 address
    // from it. This address should be the privacy address for device with type
    // of ethernet or wifi.
    std::string ipv6_address;
    std::string ipv6_gateway;
    std::vector<std::string> ipv6_dns_addresses;
  };

  // Represents the properties of an object of org.chromium.flimflam.Device.
  // Only contains the properties we care about.
  // TODO(jiejiang): add the following fields into this struct:
  // - the dbus path of the Service associated to this Device if any
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
      kPPPoE,
      kTunnel,
      kVPN,
      kWifi,
    };

    Type type;
    std::string ifname;
    std::string service_path;
    IPConfig ipconfig;
  };

  // Client callback for learning when shill default logical network changes.
  using DefaultDeviceChangeHandler =
      base::Callback<void(const Device& new_device, const Device& prev_device)>;
  // Client callback for learning which network interfaces start or stop being
  // managed by shill.
  using DevicesChangeHandler =
      base::Callback<void(const std::vector<std::string>& added,
                          const std::vector<std::string>& removed)>;
  // Client callback for listening to IPConfig changes on any shill Device with
  // interface name |ifname|.
  using IPConfigsChangeHandler =
      base::Callback<void(const std::string& ifname, const IPConfig& ipconfig)>;

  explicit ShillClient(const scoped_refptr<dbus::Bus>& bus);
  ShillClient(const ShillClient&) = delete;
  ShillClient& operator=(const ShillClient&) = delete;

  virtual ~ShillClient() = default;

  // Registers the provided handler for changes in shill default logical network
  // The handler will be called once immediately at registration
  // with the current default logical network as |new_device| and an empty
  // Device as |prev_device|.
  void RegisterDefaultDeviceChangedHandler(
      const DefaultDeviceChangeHandler& handler);

  void RegisterDevicesChangedHandler(const DevicesChangeHandler& handler);

  void RegisterIPConfigsChangedHandler(const IPConfigsChangeHandler& handler);

  void ScanDevices();

  // Fetches Device dbus properties via dbus for the shill Device with interface
  // name |ifname|. Returns false if an error occurs. Notes that this method
  // will block the current thread.
  virtual bool GetDeviceProperties(const std::string& ifname, Device* output);

  // Returns the cached interface name of the current default logical network;
  // does not initiate a property fetch.
  virtual const std::string& default_interface() const;
  // Returns the cached default logical shill Device; does not initiate a
  // property fetch.
  virtual const Device& default_device() const;
  // Returns interface names of all known shill Devices.
  const std::vector<std::string> get_interfaces() const;
  // Returns true if |ifname| is the interface name of a known shill Device.
  bool has_interface(const std::string& ifname) const;

 protected:
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  void OnDevicePropertyChangeRegistration(const std::string& interface,
                                          const std::string& signal_name,
                                          bool success);
  void OnDevicePropertyChange(const std::string& device,
                              const std::string& property_name,
                              const brillo::Any& property_value);

  // Returns the current default logical shill Device for the system, or an
  // empty shill Device result when the system has no default network.
  virtual Device GetDefaultDevice();

  // Returns the interface name of the shill Device identified by |device|, or
  // returns the empty string if it fails.
  virtual std::string GetIfname(const dbus::ObjectPath& device_path);

 private:
  void UpdateDevices(const brillo::Any& property_value);

  // Sets the internal variable tracking the system default logical network and
  // calls the registered client handlers if the default logical network
  // changed.
  void SetDefaultDevice(const Device& new_default);

  // Parses the |property_value| as the IPConfigs property of the shill Device
  // identified by |device|, which
  // should be a list of object paths of IPConfigs.
  IPConfig ParseIPConfigsProperty(const std::string& device,
                                  const brillo::Any& property_value);

  // Tracks the system default logical network chosen by shill. This corresponds
  // to the physical or virtual shill Device associated with the default logical
  // network service.
  Device default_device_;
  // Tracks all network interfaces managed by shill and maps shill Device
  // identifiers to interface names.
  std::map<std::string, std::string> devices_;
  // Stores the map from shill Device identifier to its object path in shill for
  // all the shill Devices we have seen. Unlike |devices_|, entries in this map
  // will never be removed during the lifetime of this class. We maintain this
  // map mainly for keeping track of the shill Device object proxies we have
  // created, to avoid registering the handler on the same object twice.
  std::map<std::string, dbus::ObjectPath> known_device_paths_;

  // Called when the shill Device used as the default logical network changes.
  std::vector<DefaultDeviceChangeHandler> default_device_handlers_;
  // Called when the list of network interfaces managed by shill changes.
  std::vector<DevicesChangeHandler> device_handlers_;
  // Called when the IPConfigs of any shill Device changes.
  std::vector<IPConfigsChangeHandler> ipconfigs_handlers_;

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxy> manager_proxy_;

  base::WeakPtrFactory<ShillClient> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, const ShillClient::Device& dev);
std::ostream& operator<<(std::ostream& stream,
                         const ShillClient::Device::Type type);

}  // namespace patchpanel

#endif  // PATCHPANEL_SHILL_CLIENT_H_
