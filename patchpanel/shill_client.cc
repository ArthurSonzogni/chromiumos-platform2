// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/shill_client.h"

#include <memory>
#include <optional>
#include <string_view>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/containers/fixed_flat_map.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/technology.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <shill/dbus-proxies.h>

namespace patchpanel {

namespace {

std::optional<net_base::Technology> ParseDeviceType(std::string_view type_str) {
  static constexpr auto str2enum =
      base::MakeFixedFlatMap<std::string_view, net_base::Technology>({
          {shill::kTypeCellular, net_base::Technology::kCellular},
          {shill::kTypeEthernet, net_base::Technology::kEthernet},
          {shill::kTypeEthernetEap, net_base::Technology::kEthernet},
          {shill::kTypeWifi, net_base::Technology::kWiFi},
          {shill::kTypeVPN, net_base::Technology::kVPN},
      });

  const auto iter = str2enum.find(type_str);
  if (iter == str2enum.end()) {
    return std::nullopt;
  }
  return iter->second;
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
  // By default all new non-Cellular shill Devices are active.
  if (device.technology != net_base::Technology::kCellular) {
    return true;
  }
  // b/273741099: A Cellular Device is active iff it has a primary multiplexed
  // interface.
  return device.primary_multiplexed_interface.has_value();
}

}  // namespace

bool ShillClient::Device::IsConnected() const {
  return network_config.ipv4_address.has_value() ||
         !network_config.ipv6_addresses.empty();
}

bool ShillClient::Device::IsIPv6Only() const {
  return !network_config.ipv4_address.has_value() &&
         !network_config.ipv6_addresses.empty();
}

ShillClient::ShillClient(const scoped_refptr<dbus::Bus>& bus, System* system)
    : bus_(bus), system_(system) {
  manager_proxy_ =
      std::make_unique<org::chromium::flimflam::ManagerProxy>(bus_);
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&ShillClient::OnManagerPropertyChange,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&ShillClient::OnManagerPropertyChangeRegistration,
                     weak_factory_.GetWeakPtr()));
  // Shill client needs to know about the current default devices in case the
  // default devices are available prior to the client.
  UpdateDefaultDevices();

  // Also fetch the DoH provider list.
  brillo::VariantDictionary props;
  if (!manager_proxy_->GetProperties(&props, nullptr)) {
    LOG(ERROR) << "Unable to get Manager properties";
    return;
  }
  if (const auto it = props.find(shill::kDNSProxyDOHProvidersProperty);
      it != props.end()) {
    UpdateDoHProviders(it->second);
  } else {
    LOG(ERROR) << "Manager properties is missing "
               << shill::kDNSProxyDOHProvidersProperty;
  }
}

ShillClient::~ShillClient() = default;

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

void ShillClient::UpdateNetworkConfigCache(
    int ifindex, const net_base::NetworkConfig& network_config) {
  const auto it = network_config_cache_.find(ifindex);

  bool has_changed = false;
  if (it == network_config_cache_.end()) {
    network_config_cache_.insert({ifindex, network_config});
    has_changed = true;
  } else {
    has_changed = it->second != network_config;
    if (has_changed) {
      it->second = network_config;
    }
  }

  if (has_changed) {
    OnDeviceNetworkConfigChange(ifindex);
  }
}

void ShillClient::ClearNetworkConfigCache(int ifindex) {
  if (network_config_cache_.erase(ifindex) == 1) {
    OnDeviceNetworkConfigChange(ifindex);
  }
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
  if (default_logical_device->technology != net_base::Technology::kVPN) {
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

  return GetDeviceProperties(device_path);
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
  } else if (property_name == shill::kDNSProxyDOHProvidersProperty) {
    UpdateDoHProviders(property_value);
    return;
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
  // Explicitly trigger the callback to ensure existing Devices are captured.
  handler.Run(GetDevices(), /*removed=*/{});
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
      const std::optional<ShillClient::Device> new_device =
          GetDeviceProperties(device_path);
      if (!new_device.has_value()) {
        LOG(WARNING) << "Failed to add properties of new Device "
                     << device_path.value();
        devices_.erase(device_path);
        continue;
      }
      if (!IsActiveDevice(*new_device)) {
        LOG(INFO) << "Ignoring inactive shill Device " << *new_device;
        continue;
      }
      LOG(INFO) << "New shill Device " << *new_device;
      added_devices.push_back(*new_device);
      devices_[device_path] = *new_device;
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

std::optional<ShillClient::Device> ShillClient::GetDeviceProperties(
    const dbus::ObjectPath& device_path) {
  auto output = std::make_optional<ShillClient::Device>();

  org::chromium::flimflam::DeviceProxy proxy(bus_, device_path);
  brillo::VariantDictionary props;
  if (!proxy.GetProperties(&props, nullptr)) {
    LOG(ERROR) << "Unable to get shill Device properties for "
               << device_path.value();
    return std::nullopt;
  }

  const auto& type_it = props.find(shill::kTypeProperty);
  if (type_it == props.end()) {
    LOG(ERROR) << "shill Device properties is missing Type for "
               << device_path.value();
    return std::nullopt;
  }
  const std::string& type_str = type_it->second.TryGet<std::string>();
  const std::optional<net_base::Technology> technology =
      ParseDeviceType(type_str);
  if (!technology.has_value()) {
    LOG(ERROR) << "Unknown shill Device type " << type_str << " for "
               << device_path.value();
    return std::nullopt;
  }
  output->technology = *technology;

  const auto& interface_it = props.find(shill::kInterfaceProperty);
  if (interface_it == props.end()) {
    LOG(ERROR) << "shill Device properties is missing Interface for "
               << device_path.value();
    return std::nullopt;
  }
  output->shill_device_interface_property =
      interface_it->second.TryGet<std::string>();
  output->ifname = interface_it->second.TryGet<std::string>();

  // Ensure that |primary_multiplexed_interface| is nullopt when:
  //   - kPrimaryMultiplexedInterfaceProperty is not defined for Cellular
  //   Devices,
  //   - the Device is not a Cellular Device.
  output->primary_multiplexed_interface = std::nullopt;
  if (output->technology == net_base::Technology::kCellular) {
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
    // b/267111163: ensure for Cellular Device that the network interface
    // |ifname| used for the datapath setup is set to the primary multiplexed
    // interface.
    output->ifname = output->primary_multiplexed_interface.value_or("");
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
    if (it != datapath_interface_cache_.end()) {
      output->ifname = it->second.first;
      output->ifindex = it->second.second;
    } else if (output->technology == net_base::Technology::kCellular) {
      // When a Cellular shill Device is inactive, it is expected that the
      // datapath interface name and interface index are undefined. Furthermore
      // if the Device has never been active, there is no cache entry in
      // |datapath_interface_cache_| yet.
      output->ifname = "";
      output->ifindex = -1;
    } else {
      LOG(ERROR)
          << "No datapath interface name and index entry for shill Device "
          << output->shill_device_interface_property;
      return std::nullopt;
    }
  }

  if (const auto it = network_config_cache_.find(output->ifindex);
      it != network_config_cache_.end()) {
    output->network_config = it->second;
  } else {
    output->network_config = {};
  }

  // Optional property: a Device does not necessarily have a selected Service at
  // all time.
  const auto& selected_service_it = props.find(shill::kSelectedServiceProperty);
  if (selected_service_it != props.end()) {
    output->service_path =
        selected_service_it->second.TryGet<dbus::ObjectPath>().value();
  }

  return output;
}

const ShillClient::Device* ShillClient::GetDeviceByShillDeviceName(
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

const ShillClient::Device* ShillClient::GetDeviceByIfindex(int ifindex) const {
  // To find the VPN Device, the default logical Device must be checked
  // separately.
  if (default_logical_device_ && default_logical_device_->ifindex == ifindex) {
    return default_logical_device_.operator->();
  }
  for (const auto& [_, device] : devices_) {
    if (device.ifindex == ifindex) {
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
  if (property_name == shill::kPrimaryMultiplexedInterfaceProperty) {
    OnDevicePrimaryMultiplexedInterfaceChange(
        device_path, property_value.TryGet<std::string>());
  }
}

void ShillClient::OnDevicePrimaryMultiplexedInterfaceChange(
    const dbus::ObjectPath& device_path,
    const std::string& primary_multiplexed_interface) {
  LOG(INFO) << __func__ << ": Device " << device_path.value()
            << " has primary multiplexed interface \""
            << primary_multiplexed_interface << "\"";
  const auto& device_it = devices_.find(device_path);
  if (device_it == devices_.end() && !primary_multiplexed_interface.empty()) {
    // If the shill Device is not found in |devices_| it is not active. If the
    // primary multiplexed interface is now defined, that Device is active and
    // needs to be advertised as a new Device.
    ScanDevices();
    UpdateDefaultDevices();
    // b/294053895: If the shill Device is now active, it might already be
    // connected. Make sure that IP configuration listeners are notified.
    const auto& device_it = devices_.find(device_path);
    if (device_it != devices_.end() && IsActiveDevice(device_it->second)) {
      NotifyIPConfigChangeHandlers(device_it->second);
      NotifyIPv6NetworkChangeHandlers(device_it->second, {});
    }
    return;
  }

  // The shill Device is already active the primary multiplexed interface is
  // already known, this event can be ignored.
  if (primary_multiplexed_interface ==
      device_it->second.primary_multiplexed_interface) {
    return;
  }

  // When the shill Device is already active and the primary multiplexed
  // interface property changed, it should now be empty and the shill Device
  // should not be active anymore. Refresh all properties at once and advertise
  // it as a removed Device.
  if (!primary_multiplexed_interface.empty()) {
    LOG(ERROR) << __func__ << ": Device " << device_path.value()
               << " has primary multiplexed interface \""
               << primary_multiplexed_interface << "\" but we had "
               << device_it->second;
  }
  const std::optional<Device> updated_device = GetDeviceProperties(device_path);
  if (!updated_device.has_value()) {
    LOG(ERROR) << "Failed to update properties of Device "
               << device_path.value();
    return;
  }
  device_it->second = *updated_device;
  if (!IsActiveDevice(device_it->second)) {
    ScanDevices();
    UpdateDefaultDevices();
  }
}

void ShillClient::OnDeviceNetworkConfigChange(int ifindex) {
  auto device_it = devices_.begin();
  for (; device_it != devices_.end(); device_it++) {
    if (device_it->second.ifindex == ifindex) {
      break;
    }
  }
  if (device_it == devices_.end()) {
    // If the Device is not found in |devices_| it is not active. Ignore IP
    // configuration changes until the device becomes active.
    return;
  }

  const dbus::ObjectPath& device_path = device_it->first;
  net_base::NetworkConfig old_ip_config = device_it->second.network_config;

  // Refresh all properties at once.
  const std::optional<Device> updated_device = GetDeviceProperties(device_path);
  if (!updated_device.has_value()) {
    LOG(ERROR) << "Failed to update properties of Device "
               << device_path.value();
    return;
  }
  device_it->second = *updated_device;

  // Do not run the IPConfigsChangeHandler and IPv6NetworkChangeHandler
  // callbacks if there is no IPConfig change.
  const auto& new_ip_config = device_it->second.network_config;
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
  NotifyIPConfigChangeHandlers(device_it->second);
  NotifyIPv6NetworkChangeHandlers(device_it->second,
                                  old_ip_config.ipv6_addresses);
}

void ShillClient::NotifyIPConfigChangeHandlers(const Device& device) {
  for (const auto& handler : ipconfigs_handlers_) {
    handler.Run(device);
  }
}

void ShillClient::NotifyIPv6NetworkChangeHandlers(
    const Device& device, const std::vector<net_base::IPv6CIDR>& old_cidr) {
  // Compares if the new IPv6 network is the same as the old one by checking its
  // prefix. Note that we are currently only assuming all addresses are of a
  // same prefix, and only comparing the first address.
  const auto& new_cidr = device.network_config.ipv6_addresses;
  if (old_cidr.empty() && new_cidr.empty()) {
    return;
  }
  if (!old_cidr.empty() && !new_cidr.empty() &&
      old_cidr[0].GetPrefixCIDR() == new_cidr[0].GetPrefixCIDR()) {
    return;
  }
  for (const auto& handler : ipv6_network_handlers_) {
    handler.Run(device);
  }
}

void ShillClient::RegisterDoHProvidersChangedHandler(
    const DoHProvidersChangeHandler& handler) {
  doh_provider_handlers_.push_back(handler);
  handler.Run(doh_providers_);
}

void ShillClient::UpdateDoHProviders(const brillo::Any& property_value) {
  base::flat_set<std::string> new_doh_providers;
  for (const auto& [key, _] :
       property_value.TryGet<brillo::VariantDictionary>()) {
    new_doh_providers.insert(key);
  }

  if (new_doh_providers == doh_providers_) {
    return;
  }

  doh_providers_.swap(new_doh_providers);
  for (const auto& h : doh_provider_handlers_) {
    h.Run(doh_providers_);
  }
}

std::ostream& operator<<(std::ostream& stream, const ShillClient::Device& dev) {
  stream << "{shill_device: " << dev.shill_device_interface_property
         << ", type: "
         << (dev.technology.has_value() ? ToString(*dev.technology)
                                        : "Unknown");
  if (dev.technology == net_base::Technology::kCellular) {
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

}  // namespace patchpanel
