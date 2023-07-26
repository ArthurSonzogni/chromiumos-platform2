// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/shill_client.h"

#include <algorithm>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_path.h>
#include <net-base/ip_address.h>

namespace patchpanel {

namespace {

ShillClient::Device::Type ParseDeviceType(const std::string& type_str) {
  static const std::map<std::string, ShillClient::Device::Type> str2enum{
      {shill::kTypeCellular, ShillClient::Device::Type::kCellular},
      {shill::kTypeEthernet, ShillClient::Device::Type::kEthernet},
      {shill::kTypeEthernetEap, ShillClient::Device::Type::kEthernetEap},
      {shill::kTypeGuestInterface, ShillClient::Device::Type::kGuestInterface},
      {shill::kTypeLoopback, ShillClient::Device::Type::kLoopback},
      {shill::kTypePPP, ShillClient::Device::Type::kPPP},
      {shill::kTypeTunnel, ShillClient::Device::Type::kTunnel},
      {shill::kTypeWifi, ShillClient::Device::Type::kWifi},
      {shill::kTypeVPN, ShillClient::Device::Type::kVPN},
  };

  const auto it = str2enum.find(type_str);
  return it != str2enum.end() ? it->second
                              : ShillClient::Device::Type::kUnknown;
}

const std::string DeviceTypeName(ShillClient::Device::Type type) {
  static const std::map<ShillClient::Device::Type, std::string> enum2str{
      {ShillClient::Device::Type::kUnknown, "Unknown"},
      {ShillClient::Device::Type::kCellular, "Cellular"},
      {ShillClient::Device::Type::kEthernet, "Ethernet"},
      {ShillClient::Device::Type::kEthernetEap, "EthernetEap"},
      {ShillClient::Device::Type::kGuestInterface, "GuestInterface"},
      {ShillClient::Device::Type::kLoopback, "Loopback"},
      {ShillClient::Device::Type::kPPP, "PPP"},
      {ShillClient::Device::Type::kTunnel, "Tunnel"},
      {ShillClient::Device::Type::kVPN, "VPN"},
      {ShillClient::Device::Type::kWifi, "Wifi"},
  };

  const auto it = enum2str.find(type);
  return it != enum2str.end() ? it->second : "Unknown";
}

void RunDefaultNetworkListeners(
    const std::optional<ShillClient::Device>& new_device,
    const std::optional<ShillClient::Device>& prev_device,
    const std::vector<ShillClient::DefaultDeviceChangeHandler>& listeners) {
  const auto* new_p = new_device ? new_device.operator->() : nullptr;
  const auto* prev_p = prev_device ? prev_device.operator->() : nullptr;
  for (const auto& h : listeners) {
    if (!h.is_null()) {
      h.Run(new_p, prev_p);
    }
  }
}

bool IsActiveDevice(const ShillClient::Device& device) {
  // TODO(b/273741099): For Cellular Device, check if the primary multiplexed
  // interface is known.
  return true;
}
}  // namespace

bool ShillClient::Device::IsConnected() const {
  return ipconfig.ipv4_cidr || ipconfig.ipv6_cidr;
}

ShillClient::ShillClient(const scoped_refptr<dbus::Bus>& bus, System* system)
    : bus_(bus), system_(system) {
  manager_proxy_.reset(new org::chromium::flimflam::ManagerProxy(bus_));
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&ShillClient::OnManagerPropertyChange,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&ShillClient::OnManagerPropertyChangeRegistration,
                     weak_factory_.GetWeakPtr()));
  // Shill client needs to know about the current default devices in case the
  // default devices are available prior to the client.
  UpdateDefaultDevices();
}

const ShillClient::Device* ShillClient::default_logical_device() const {
  if (!default_logical_device_) {
    return nullptr;
  }
  return default_logical_device_.operator->();
}

const ShillClient::Device* ShillClient::default_physical_device() const {
  if (!default_physical_device_) {
    return nullptr;
  }
  return default_physical_device_.operator->();
}

const std::vector<ShillClient::Device> ShillClient::GetDevices() const {
  std::vector<Device> devices;
  devices.reserve(devices_.size());
  for (const auto& [_, device] : devices_) {
    devices.push_back(device);
  }
  return devices;
}

void ShillClient::ScanDevices() {
  brillo::VariantDictionary props;
  if (!manager_proxy_->GetProperties(&props, nullptr)) {
    LOG(ERROR) << "Unable to get Manager properties";
    return;
  }
  const auto it = props.find(shill::kDevicesProperty);
  if (it == props.end()) {
    LOG(WARNING) << "Manager properties is missing " << shill::kDevicesProperty;
    return;
  }
  UpdateDevices(it->second);
}

void ShillClient::UpdateDefaultDevices() {
  // Iterate through Services listed as the shill Manager "Services" properties.
  // This Service DBus path list is built in shill with the Manager function
  // EnumerateAvailableServices() which uses the vector of Services with the
  // Service::Compare() function. This guarantees that connected Services are at
  // the front of the list. If a VPN Service is connected, it is always at the
  // front of the list, however this relies on the following implementation
  // details:
  //   - portal detection is not run on VPN, therefore a connected VPN should
  //     always be in the "online" state.
  //   - the shill Manager Technology order property has VPN in front
  //     (Manager.GetServiceOrder).
  const auto services = GetServices();
  if (services.empty()) {
    SetDefaultLogicalDevice(std::nullopt);
    SetDefaultPhysicalDevice(std::nullopt);
    return;
  }
  auto default_logical_device = GetDeviceFromServicePath(services[0]);
  if (!default_logical_device) {
    SetDefaultLogicalDevice(std::nullopt);
    SetDefaultPhysicalDevice(std::nullopt);
    return;
  }
  if (!IsActiveDevice(*default_logical_device)) {
    SetDefaultLogicalDevice(std::nullopt);
    SetDefaultPhysicalDevice(std::nullopt);
    return;
  }
  SetDefaultLogicalDevice(default_logical_device);

  // No VPN connection, the logical and physical Devices are the same.
  if (default_logical_device->type != ShillClient::Device::Type::kVPN) {
    SetDefaultPhysicalDevice(default_logical_device);
    return;
  }

  // In case of a VPN, also get the physical Device properties.
  if (services.size() < 2) {
    LOG(ERROR) << "No physical Service found";
    SetDefaultPhysicalDevice(std::nullopt);
    return;
  }
  auto default_physical_device = GetDeviceFromServicePath(services[1]);
  if (!default_physical_device) {
    LOG(ERROR) << "Could not update the default physical Device";
    SetDefaultPhysicalDevice(std::nullopt);
    return;
  }
  if (!IsActiveDevice(*default_physical_device)) {
    LOG(ERROR) << default_physical_device << " found for Service "
               << services[1].value()
               << " is not active, but a VPN was a connected";
    SetDefaultPhysicalDevice(std::nullopt);
    return;
  }
  SetDefaultPhysicalDevice(default_physical_device);
}

std::vector<dbus::ObjectPath> ShillClient::GetServices() {
  brillo::VariantDictionary manager_properties;
  if (!manager_proxy_->GetProperties(&manager_properties, nullptr)) {
    LOG(ERROR) << "Unable to get Manager properties";
    return {};
  }
  return brillo::GetVariantValueOrDefault<std::vector<dbus::ObjectPath>>(
      manager_properties, shill::kServicesProperty);
}

std::optional<ShillClient::Device> ShillClient::GetDeviceFromServicePath(
    const dbus::ObjectPath& service_path) {
  brillo::VariantDictionary service_properties;
  org::chromium::flimflam::ServiceProxy service_proxy(bus_, service_path);
  if (!service_proxy.GetProperties(&service_properties, nullptr)) {
    LOG(ERROR) << "Unable to get Service properties for "
               << service_path.value();
    return std::nullopt;
  }

  // Check if there is any connected Service at the moment.
  if (const auto it = service_properties.find(shill::kIsConnectedProperty);
      it == service_properties.end()) {
    LOG(ERROR) << "Service " << service_path.value() << " missing property "
               << shill::kIsConnectedProperty;
    return std::nullopt;
  } else if (!it->second.TryGet<bool>()) {
    // There is no default Device if there is no connected Service.
    LOG(INFO) << "Service " << service_path.value() << " was not connected";
    return std::nullopt;
  }

  auto device_path = brillo::GetVariantValueOrDefault<dbus::ObjectPath>(
      service_properties, shill::kDeviceProperty);
  if (!device_path.IsValid()) {
    LOG(ERROR) << "Service " << service_path.value() << " missing property "
               << shill::kDeviceProperty;
    return std::nullopt;
  }

  Device device = {};
  if (!GetDeviceProperties(device_path, &device)) {
    return std::nullopt;
  }
  return device;
}

void ShillClient::OnManagerPropertyChangeRegistration(
    const std::string& interface,
    const std::string& signal_name,
    bool success) {
  if (!success)
    LOG(FATAL) << "Unable to register for interface change events";
}

void ShillClient::OnManagerPropertyChange(const std::string& property_name,
                                          const brillo::Any& property_value) {
  if (property_name == shill::kDevicesProperty) {
    UpdateDevices(property_value);
  } else if (property_name != shill::kDefaultServiceProperty &&
             property_name != shill::kServicesProperty &&
             property_name != shill::kConnectionStateProperty) {
    return;
  }

  // All registered DefaultDeviceChangeHandler objects should be called if
  // the default network has changed or if shill::kDevicesProperty has changed.
  UpdateDefaultDevices();
}

void ShillClient::SetDefaultLogicalDevice(const std::optional<Device>& device) {
  if (!default_logical_device_ && !device) {
    return;
  }
  if (default_logical_device_ && device &&
      default_logical_device_->ifname == device->ifname) {
    return;
  }
  LOG(INFO) << "Default logical device changed from " << default_logical_device_
            << " to " << device;
  RunDefaultNetworkListeners(device, default_logical_device_,
                             default_logical_device_handlers_);
  default_logical_device_ = device;
}

void ShillClient::SetDefaultPhysicalDevice(
    const std::optional<Device>& device) {
  if (!default_physical_device_ && !device) {
    return;
  }
  if (default_physical_device_ && device &&
      default_physical_device_->ifname == device->ifname) {
    return;
  }
  LOG(INFO) << "Default physical device changed from "
            << default_physical_device_ << " to " << device;
  RunDefaultNetworkListeners(device, default_physical_device_,
                             default_physical_device_handlers_);
  default_physical_device_ = device;
}

void ShillClient::RegisterDefaultLogicalDeviceChangedHandler(
    const DefaultDeviceChangeHandler& handler) {
  default_logical_device_handlers_.emplace_back(handler);
  // Explicitly trigger the callback once to let it know of the the current
  // default interface. The previous interface is left empty.
  if (default_logical_device_) {
    handler.Run(default_logical_device_.operator->(), nullptr);
  }
}

void ShillClient::RegisterDefaultPhysicalDeviceChangedHandler(
    const DefaultDeviceChangeHandler& handler) {
  default_physical_device_handlers_.emplace_back(handler);
  // Explicitly trigger the callback once to let it know of the the current
  // default interface. The previous interface is left empty.
  if (default_physical_device_) {
    handler.Run(default_physical_device_.operator->(), nullptr);
  }
}

void ShillClient::RegisterDevicesChangedHandler(
    const DevicesChangeHandler& handler) {
  device_handlers_.emplace_back(handler);
}

void ShillClient::RegisterIPConfigsChangedHandler(
    const IPConfigsChangeHandler& handler) {
  ipconfigs_handlers_.emplace_back(handler);
}

void ShillClient::RegisterIPv6NetworkChangedHandler(
    const IPv6NetworkChangeHandler& handler) {
  ipv6_network_handlers_.emplace_back(handler);
}

void ShillClient::UpdateDevices(const brillo::Any& property_value) {
  // All current shill Devices advertised by shill. This set is used
  // for finding Devices removed by shill and contains both active and inactive
  // Devices.
  std::set<dbus::ObjectPath> current;

  // Find all new active shill Devices not yet tracked by patchpanel.
  std::vector<Device> added_devices;
  for (const auto& device_path :
       property_value.TryGet<std::vector<dbus::ObjectPath>>()) {
    current.insert(device_path);

    // Registers handler if we see this shill Device for the first time.
    if (known_device_paths_.insert(device_path).second) {
      org::chromium::flimflam::DeviceProxy proxy(bus_, device_path);
      proxy.RegisterPropertyChangedSignalHandler(
          base::BindRepeating(&ShillClient::OnDevicePropertyChange,
                              weak_factory_.GetWeakPtr(), device_path),
          base::BindOnce(&ShillClient::OnDevicePropertyChangeRegistration,
                         weak_factory_.GetWeakPtr()));
    }

    // Populate ShillClient::Device properties for any new active shill Device.
    if (!base::Contains(devices_, device_path)) {
      ShillClient::Device new_device;
      if (!GetDeviceProperties(device_path, &new_device)) {
        LOG(WARNING) << "Failed to add properties of new Device "
                     << device_path.value();
        devices_.erase(device_path);
        continue;
      }
      if (!IsActiveDevice(new_device)) {
        LOG(INFO) << "Ignoring inactive shill Device " << new_device;
        continue;
      }
      LOG(INFO) << "New shill Device " << new_device;
      added_devices.push_back(new_device);
      devices_[device_path] = new_device;
    }
  }

  // Find all shill Devices removed by shill or shill Devices that became
  // inactive and remove them from |devices_|,
  std::vector<Device> removed_devices;
  for (auto it = devices_.begin(); it != devices_.end();) {
    if (!base::Contains(current, it->first) || !IsActiveDevice(it->second)) {
      LOG(INFO) << "Removed shill Device " << it->second;
      removed_devices.push_back(it->second);
      it = devices_.erase(it);
    } else {
      it++;
    }
  }

  // This can happen if:
  //   - The default network switched from one device to another.
  //   - An inactive Device is removed by shill and it was already ignored by
  //   ShillClient.
  //   - A Device is added by shill but not yet considered active, and should be
  //   ignored by ShillClient.
  if (added_devices.empty() && removed_devices.empty()) {
    return;
  }

  // Update DevicesChangeHandler listeners.
  for (const auto& h : device_handlers_) {
    h.Run(added_devices, removed_devices);
  }
}

ShillClient::IPConfig ShillClient::ParseIPConfigsProperty(
    const dbus::ObjectPath& device, const brillo::Any& ipconfig_paths) {
  IPConfig ipconfig;
  for (const auto& path :
       ipconfig_paths.TryGet<std::vector<dbus::ObjectPath>>()) {
    std::unique_ptr<org::chromium::flimflam::IPConfigProxy> ipconfig_proxy(
        new org::chromium::flimflam::IPConfigProxy(bus_, path));
    brillo::VariantDictionary ipconfig_props;

    if (!ipconfig_proxy->GetProperties(&ipconfig_props, nullptr)) {
      // It is possible that an IPConfig object is removed after we know its
      // path, especially when the interface is going down.
      LOG(WARNING) << "[" << device.value() << "]: "
                   << "Unable to get properties for " << path.value();
      continue;
    }

    // Gets the value of address, prefix_length, gateway, and dns_servers.
    auto it = ipconfig_props.find(shill::kAddressProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device.value()
                   << "]: IPConfig properties is missing Address";
      continue;
    }
    const std::string& address_str = it->second.TryGet<std::string>();
    if (address_str.empty()) {
      // On IPv6 only networks, dhcp is expected to fail, nevertheless shill
      // will still expose a mostly empty IPConfig object. On dual stack
      // networks, the IPv6 configuration may be available before dhcp has
      // finished. Avoid logging spurious WARNING messages in these two cases.
      continue;
    }

    it = ipconfig_props.find(shill::kPrefixlenProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device.value() << "]: "
                   << " IPConfig properties is missing Prefixlen";
      continue;
    }
    int prefix_length = it->second.TryGet<int>();
    if (prefix_length == 0) {
      LOG(WARNING)
          << "[" << device.value() << "]: "
          << " IPConfig Prefixlen property is 0, may be an invalid setup";
    }

    const auto cidr =
        net_base::IPCIDR::CreateFromStringAndPrefix(address_str, prefix_length);
    if (!cidr) {
      LOG(WARNING) << "[" << device.value()
                   << "]: IPConfig Address and Prefixlen property was invalid: "
                   << address_str << "/" << prefix_length;
      continue;
    }
    const bool is_ipv4 = (cidr->GetFamily() == net_base::IPFamily::kIPv4);
    const std::string method = is_ipv4 ? "IPv4" : "IPv6";
    if ((is_ipv4 && ipconfig.ipv4_cidr) || (!is_ipv4 && ipconfig.ipv6_cidr)) {
      LOG(WARNING) << "[" << device.value() << "]: "
                   << "Duplicated IPconfig for " << method;
      continue;
    }

    it = ipconfig_props.find(shill::kGatewayProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device.value() << "]: " << method
                   << " IPConfig properties is missing Gateway";
      continue;
    }
    const std::string& gateway = it->second.TryGet<std::string>();
    if (gateway.empty()) {
      LOG(WARNING) << "[" << device.value() << "]: " << method
                   << " IPConfig Gateway property was empty.";
      continue;
    }

    it = ipconfig_props.find(shill::kNameServersProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device.value() << "]: " << method
                   << " IPConfig properties is missing NameServers";
      // Shill will emit this property with empty value if it has no dns for
      // this device, so missing this property indicates an error.
      continue;
    }
    const std::vector<std::string>& dns_addresses =
        it->second.TryGet<std::vector<std::string>>();

    // Fills the IPConfig struct according to the type.
    if (is_ipv4) {
      ipconfig.ipv4_cidr = cidr->ToIPv4CIDR();
      ipconfig.ipv4_gateway = net_base::IPv4Address::CreateFromString(gateway);
      if (!ipconfig.ipv4_gateway) {
        LOG(WARNING) << "[" << device.value() << "]: " << method
                     << " IPConfig Gateway property was not valid IPv4Address: "
                     << gateway;
      }
      ipconfig.ipv4_dns_addresses = dns_addresses;
    } else {  // AF_INET6
      ipconfig.ipv6_cidr = cidr->ToIPv6CIDR();
      ipconfig.ipv6_gateway = net_base::IPv6Address::CreateFromString(gateway);
      if (!ipconfig.ipv6_gateway) {
        LOG(WARNING) << "[" << device.value() << "]: " << method
                     << " IPConfig Gateway property was not valid IPv6Address: "
                     << gateway;
      }
      ipconfig.ipv6_dns_addresses = dns_addresses;
    }
  }

  return ipconfig;
}

bool ShillClient::GetDeviceProperties(const dbus::ObjectPath& device_path,
                                      Device* output) {
  DCHECK(output);

  org::chromium::flimflam::DeviceProxy proxy(bus_, device_path);
  brillo::VariantDictionary props;
  if (!proxy.GetProperties(&props, nullptr)) {
    LOG(ERROR) << "Unable to get shill Device properties for "
               << device_path.value();
    return false;
  }

  const auto& type_it = props.find(shill::kTypeProperty);
  if (type_it == props.end()) {
    LOG(ERROR) << "shill Device properties is missing Type for "
               << device_path.value();
    return false;
  }
  const std::string& type_str = type_it->second.TryGet<std::string>();
  output->type = ParseDeviceType(type_str);
  if (output->type == Device::Type::kUnknown) {
    LOG(ERROR) << "Unknown shill Device type " << type_str << " for "
               << device_path.value();
    return false;
  }

  const auto& interface_it = props.find(shill::kInterfaceProperty);
  if (interface_it == props.end()) {
    LOG(ERROR) << "shill Device properties is missing Interface for "
               << device_path.value();
    return false;
  }
  output->shill_device_interface_property =
      interface_it->second.TryGet<std::string>();
  output->ifname = interface_it->second.TryGet<std::string>();

  // Ensure that |primary_multiplexed_interface| is nullopt when not defined.
  output->primary_multiplexed_interface = std::nullopt;
  if (output->type == Device::Type::kCellular) {
    const auto& it = props.find(shill::kPrimaryMultiplexedInterfaceProperty);
    if (it == props.end()) {
      LOG(WARNING) << "shill Cellular Device properties is missing "
                   << shill::kPrimaryMultiplexedInterfaceProperty << " for "
                   << device_path.value();
    } else {
      const auto& primary_multiplexed_interface =
          it->second.TryGet<std::string>();
      if (!primary_multiplexed_interface.empty()) {
        output->primary_multiplexed_interface = primary_multiplexed_interface;
      }
    }
  }

  // When the datapath interface exists and has an interface index, cache the
  // datapath interface name |ifname| and interface index |ifindex| keyed by the
  // shill Device property (|shill_device_interface_property|). For Cellular
  // Devices this ensures that the name of the primary multiplexed interface is
  // known after the network has disconnected. Knowing the datapath interface
  // name is necessary for mutliple cleanup operations. If the interface index
  // cannot be obtained from the kernel, look up the cache to obtain the
  // interface name and datapath interface index from the cache.
  output->ifindex = system_->IfNametoindex(output->ifname);
  if (output->ifindex > 0) {
    datapath_interface_cache_[output->shill_device_interface_property] = {
        output->ifname, output->ifindex};
  } else {
    const auto it =
        datapath_interface_cache_.find(output->shill_device_interface_property);
    if (it == datapath_interface_cache_.end()) {
      LOG(ERROR)
          << "No datapath interface name and index entry for shill Device "
          << output->shill_device_interface_property;
      return false;
    }
    output->ifname = it->second.first;
    output->ifindex = it->second.second;
  }

  const auto& ipconfigs_it = props.find(shill::kIPConfigsProperty);
  if (ipconfigs_it == props.end()) {
    LOG(ERROR) << "shill Device properties is missing IPConfigs for "
               << device_path.value();
    return false;
  }
  output->ipconfig = ParseIPConfigsProperty(device_path, ipconfigs_it->second);

  // Optional property: a Device does not necessarily have a selected Service at
  // all time.
  const auto& selected_service_it = props.find(shill::kSelectedServiceProperty);
  if (selected_service_it != props.end()) {
    output->service_path =
        selected_service_it->second.TryGet<dbus::ObjectPath>().value();
  }

  return true;
}

const ShillClient::Device* ShillClient::GetDevice(
    const std::string& shill_device_interface_property) const {
  // To find the VPN Device, the default logical Device must be checked
  // separately.
  if (default_logical_device_ &&
      default_logical_device_->shill_device_interface_property ==
          shill_device_interface_property) {
    return default_logical_device_.operator->();
  }
  for (const auto& [_, device] : devices_) {
    if (device.shill_device_interface_property ==
        shill_device_interface_property) {
      return &device;
    }
  }
  return nullptr;
}

void ShillClient::OnDevicePropertyChangeRegistration(
    const std::string& dbus_interface_name,
    const std::string& signal_name,
    bool success) {
  if (!success)
    LOG(ERROR) << "Unable to register Device property listener for "
               << signal_name;
}

void ShillClient::OnDevicePropertyChange(const dbus::ObjectPath& device_path,
                                         const std::string& property_name,
                                         const brillo::Any& property_value) {
  if (property_name != shill::kIPConfigsProperty &&
      property_name != shill::kPrimaryMultiplexedInterfaceProperty) {
    return;
  }

  const auto& device_it = devices_.find(device_path);
  if (device_it == devices_.end()) {
    // If the Device is not found in |devices_| it is not active. If a property
    // update is received for an inactive Device, that Device may now be active
    // and needs to be advertised as a new Device.
    ScanDevices();
    UpdateDefaultDevices();
    return;
  }

  IPConfig old_ip_config = device_it->second.ipconfig;

  // Refresh all properties at once.
  if (!GetDeviceProperties(device_path, &device_it->second)) {
    LOG(ERROR) << "Failed to update properties of Device "
               << device_path.value();
    return;
  }

  // If the Device is not active anymore, it needs to be advertised as a removed
  // Device and removed from |deviecs_|.
  if (!IsActiveDevice(device_it->second)) {
    ScanDevices();
    UpdateDefaultDevices();
    return;
  }

  // Do not run the IPConfigsChangeHandler and IPv6NetworkChangeHandler
  // callbacks if there is no IPConfig change.
  const IPConfig& new_ip_config = device_it->second.ipconfig;
  if (old_ip_config == new_ip_config) {
    return;
  }

  // Ensure that the cached states of the default physical Device and default
  // logical Device are refreshed as well.
  // TODO(b/273741099): Handle the VPN Device. Since the VPN Device is not
  // exposed in kDevicesProperty, ShillClient never registers a signal handler
  // for Device property changes on the VPN Device.
  if (default_physical_device_ &&
      default_physical_device_->ifname == device_it->second.ifname) {
    default_physical_device_ = device_it->second;
  }
  if (default_logical_device_ &&
      default_logical_device_->ifname == device_it->second.ifname) {
    default_logical_device_ = device_it->second;
  }

  LOG(INFO) << "[" << device_path.value()
            << "]: IPConfig changed: " << new_ip_config;
  for (const auto& handler : ipconfigs_handlers_) {
    handler.Run(device_it->second);
  }

  // Compares if the new IPv6 network is the same as the old one by checking
  // its prefix.
  const auto& old_cidr = old_ip_config.ipv6_cidr;
  const auto& new_cidr = new_ip_config.ipv6_cidr;
  if (!old_cidr && !new_cidr) {
    return;
  }
  if (old_cidr && new_cidr &&
      old_cidr->GetPrefixCIDR() == new_cidr->GetPrefixCIDR()) {
    return;
  }

  for (const auto& handler : ipv6_network_handlers_) {
    handler.Run(device_it->second);
  }
}

std::ostream& operator<<(std::ostream& stream, const ShillClient::Device& dev) {
  stream << "{shill_device: " << dev.shill_device_interface_property
         << ", type: " << DeviceTypeName(dev.type);
  if (dev.type == ShillClient::Device::Type::kCellular) {
    stream << ", primary_multiplexed_interface: "
           << dev.primary_multiplexed_interface.value_or("none");
  }
  return stream << ", ifname: " << dev.ifname << ", ifindex: " << dev.ifindex
                << ", service: " << dev.service_path << "}";
}

std::ostream& operator<<(std::ostream& stream,
                         const std::optional<ShillClient::Device>& dev) {
  if (!dev) {
    return stream << "none";
  }
  return stream << *dev;
}

std::ostream& operator<<(std::ostream& stream, const ShillClient::Device* dev) {
  if (!dev) {
    return stream << "none";
  }
  return stream << *dev;
}

std::ostream& operator<<(std::ostream& stream,
                         const ShillClient::Device::Type type) {
  return stream << DeviceTypeName(type);
}

std::ostream& operator<<(std::ostream& stream,
                         const ShillClient::IPConfig& ipconfig) {
  return stream << "{ ipv4_cidr: "
                << ipconfig.ipv4_cidr.value_or(net_base::IPv4CIDR())
                << ", ipv4_gateway: "
                << ipconfig.ipv4_gateway.value_or(net_base::IPv4Address())
                << ", ipv4_dns: ["
                << base::JoinString(ipconfig.ipv4_dns_addresses, ",")
                << "], ipv6_cidr: "
                << ipconfig.ipv6_cidr.value_or(net_base::IPv6CIDR())
                << ", ipv6_gateway: "
                << ipconfig.ipv6_gateway.value_or(net_base::IPv6Address())
                << ", ipv6_dns: ["
                << base::JoinString(ipconfig.ipv6_dns_addresses, ",") << "]}";
}
}  // namespace patchpanel
