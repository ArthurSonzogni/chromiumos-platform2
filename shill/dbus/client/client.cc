// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/client/client.h"

#include <set>
#include <string>
#include <string_view>

#include <base/containers/fixed_flat_map.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/net-base/ip_address.h>
#include <dbus/shill/dbus-constants.h>

using org::chromium::flimflam::DeviceProxy;
using org::chromium::flimflam::DeviceProxyInterface;
using org::chromium::flimflam::ManagerProxy;
using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ServiceProxy;
using org::chromium::flimflam::ServiceProxyInterface;

namespace shill {
namespace {

Client::Device::Type ParseDeviceType(std::string_view type_str) {
  static constexpr auto str2enum =
      base::MakeFixedFlatMap<std::string_view, Client::Device::Type>({
          {shill::kTypeCellular, Client::Device::Type::kCellular},
          {shill::kTypeEthernet, Client::Device::Type::kEthernet},
          {shill::kTypeEthernetEap, Client::Device::Type::kEthernetEap},
          {shill::kTypeGuestInterface, Client::Device::Type::kGuestInterface},
          {shill::kTypeLoopback, Client::Device::Type::kLoopback},
          {shill::kTypePPP, Client::Device::Type::kPPP},
          {shill::kTypeTunnel, Client::Device::Type::kTunnel},
          {shill::kTypeWifi, Client::Device::Type::kWifi},
          {shill::kTypeVPN, Client::Device::Type::kVPN},
      });

  const auto it = str2enum.find(type_str);
  return it != str2enum.end() ? it->second : Client::Device::Type::kUnknown;
}

Client::Device::ConnectionState ParseConnectionState(std::string_view s) {
  static constexpr auto m =
      base::MakeFixedFlatMap<std::string_view, Client::Device::ConnectionState>(
          {
              {shill::kStateIdle, Client::Device::ConnectionState::kIdle},
              {shill::kStateAssociation,
               Client::Device::ConnectionState::kAssociation},
              {shill::kStateConfiguration,
               Client::Device::ConnectionState::kConfiguration},
              {shill::kStateReady, Client::Device::ConnectionState::kReady},
              {shill::kStateNoConnectivity,
               Client::Device::ConnectionState::kNoConnectivity},
              {shill::kStateRedirectFound,
               Client::Device::ConnectionState::kRedirectFound},
              {shill::kStatePortalSuspected,
               Client::Device::ConnectionState::kPortalSuspected},
              {shill::kStateOnline, Client::Device::ConnectionState::kOnline},
              {shill::kStateFailure, Client::Device::ConnectionState::kFailure},
              {shill::kStateDisconnecting,
               Client::Device::ConnectionState::kDisconnecting},
          });
  const auto it = m.find(s);
  return it != m.end() ? it->second : Client::Device::ConnectionState::kUnknown;
}

std::string_view ToString(Client::Device::ConnectionState state) {
  static constexpr auto m =
      base::MakeFixedFlatMap<Client::Device::ConnectionState, std::string_view>(
          {
              {Client::Device::ConnectionState::kIdle, shill::kStateIdle},
              {Client::Device::ConnectionState::kAssociation,
               shill::kStateAssociation},
              {Client::Device::ConnectionState::kConfiguration,
               shill::kStateConfiguration},
              {Client::Device::ConnectionState::kReady, shill::kStateReady},
              {Client::Device::ConnectionState::kNoConnectivity,
               shill::kStateNoConnectivity},
              {Client::Device::ConnectionState::kRedirectFound,
               shill::kStateRedirectFound},
              {Client::Device::ConnectionState::kPortalSuspected,
               shill::kStatePortalSuspected},
              {Client::Device::ConnectionState::kOnline, shill::kStateOnline},
              {Client::Device::ConnectionState::kFailure, shill::kStateFailure},
              {Client::Device::ConnectionState::kDisconnecting,
               shill::kStateDisconnecting},
          });
  const auto it = m.find(state);
  return it != m.end() ? it->second : "unknown";
}

bool IsConnectedState(Client::Device::ConnectionState state) {
  switch (state) {
    case Client::Device::ConnectionState::kUnknown:
    case Client::Device::ConnectionState::kIdle:
    case Client::Device::ConnectionState::kAssociation:
    case Client::Device::ConnectionState::kConfiguration:
    case Client::Device::ConnectionState::kFailure:
    case Client::Device::ConnectionState::kDisconnecting:
      return false;
    case Client::Device::ConnectionState::kReady:
    case Client::Device::ConnectionState::kNoConnectivity:
    case Client::Device::ConnectionState::kRedirectFound:
    case Client::Device::ConnectionState::kPortalSuspected:
    case Client::Device::ConnectionState::kOnline:
      return true;
  }
}

std::string GetCellularProviderCountryCode(
    const brillo::VariantDictionary& device_properties) {
  auto operator_info =
      brillo::GetVariantValueOrDefault<std::map<std::string, std::string>>(
          device_properties, kHomeProviderProperty);
  return operator_info[shill::kOperatorCountryKey];
}

// Similar to brillo::GetVariantValueOrDefault() which returns the default value
// of T on failure, with an additional ERROR log.
template <typename T>
T GetVariant(const brillo::VariantDictionary& props, std::string_view key) {
  const auto it = props.find(key);
  if (it == props.end()) {
    LOG(ERROR) << key << " is not found in props";
    return T();
  }
  if (!it->second.IsTypeCompatible<T>()) {
    LOG(ERROR) << key << " has unexpected type";
    return T();
  }
  return it->second.Get<T>();
}

// Parses the value of NetworkConfig property in a best-effort way. If there is
// a failure, log it and continue the parsing.
Client::NetworkConfig ParseNetworkConfigProperty(
    const brillo::VariantDictionary& props) {
  Client::NetworkConfig ret;
  if (props.empty()) {
    return ret;
  }

  // IPv4 address.
  if (const auto val =
          GetVariant<std::string>(props, kNetworkConfigIPv4AddressProperty);
      !val.empty()) {
    ret.ipv4_address = net_base::IPv4CIDR::CreateFromCIDRString(val);
    if (!ret.ipv4_address) {
      LOG(ERROR) << "Failed to parse " << kNetworkConfigIPv4AddressProperty
                 << " value " << val;
    }
  }

  // IPv4 gateway.
  if (const auto val =
          GetVariant<std::string>(props, kNetworkConfigIPv4GatewayProperty);
      !val.empty()) {
    ret.ipv4_gateway = net_base::IPv4Address::CreateFromString(val);
    if (!ret.ipv4_gateway) {
      LOG(ERROR) << "Failed to parse " << kNetworkConfigIPv4GatewayProperty
                 << " value " << val;
    }
  }

  // IPv6 addresses.
  for (const auto& val : GetVariant<std::vector<std::string>>(
           props, kNetworkConfigIPv6AddressesProperty)) {
    auto ipv6_cidr = net_base::IPv6CIDR::CreateFromCIDRString(val);
    if (!ipv6_cidr) {
      LOG(ERROR) << "Failed to parse " << kNetworkConfigIPv6AddressesProperty
                 << " value " << val;
      continue;
    }
    ret.ipv6_addresses.push_back(*ipv6_cidr);
  }

  // IPv6 gateway.
  if (const auto val =
          GetVariant<std::string>(props, kNetworkConfigIPv6GatewayProperty);
      !val.empty()) {
    ret.ipv6_gateway = net_base::IPv6Address::CreateFromString(val);
    if (!ret.ipv6_gateway) {
      LOG(ERROR) << "Failed to parse " << kNetworkConfigIPv6GatewayProperty
                 << " value " << val;
    }
  }

  // DNS servers.
  for (const auto& val : GetVariant<std::vector<std::string>>(
           props, kNetworkConfigNameServersProperty)) {
    auto ip_addr = net_base::IPAddress::CreateFromString(val);
    if (!ip_addr) {
      LOG(ERROR) << "Failed to parse " << kNetworkConfigNameServersProperty
                 << " value " << val;
      continue;
    }
    if (ip_addr->IsZero()) {
      // Empty DNS servers are not meaningful for the clients. Skip them here.
      continue;
    }
    ret.dns_servers.push_back(*ip_addr);
  }

  // Search domains.
  ret.dns_search_domains = GetVariant<std::vector<std::string>>(
      props, kNetworkConfigSearchDomainsProperty);

  return ret;
}

}  // namespace

Client::NetworkConfig::NetworkConfig() = default;
Client::NetworkConfig::~NetworkConfig() = default;
Client::NetworkConfig::NetworkConfig(const Client::NetworkConfig& other) =
    default;
Client::NetworkConfig& Client::NetworkConfig::operator=(
    const NetworkConfig& other) = default;
bool Client::NetworkConfig::operator==(const NetworkConfig& rhs) const =
    default;

Client::Client(scoped_refptr<dbus::Bus> bus) : bus_(bus) {
  bus_->GetObjectProxy(kFlimflamServiceName, dbus::ObjectPath{"/"})
      ->SetNameOwnerChangedCallback(base::BindRepeating(
          &Client::OnOwnerChange, weak_factory_.GetWeakPtr()));
  manager_proxy_ = std::make_unique<ManagerProxy>(bus_);
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&Client::OnManagerPropertyChange,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&Client::OnManagerPropertyChangeRegistration,
                     weak_factory_.GetWeakPtr()));
}

void Client::NewDefaultServiceProxy(const dbus::ObjectPath& service_path) {
  default_service_proxy_ = std::make_unique<ServiceProxy>(bus_, service_path);
}

void Client::SetupDefaultServiceProxy(const dbus::ObjectPath& service_path) {
  NewDefaultServiceProxy(service_path);
  default_service_proxy_->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&Client::OnDefaultServicePropertyChange,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&Client::OnDefaultServicePropertyChangeRegistration,
                     weak_factory_.GetWeakPtr()));
}

void Client::ReleaseDefaultServiceProxy() {
  default_device_path_.clear();

  if (default_service_proxy_) {
    bus_->RemoveObjectProxy(kFlimflamServiceName,
                            default_service_proxy_->GetObjectPath(),
                            base::DoNothing());
    default_service_proxy_.reset();
  }
}

std::unique_ptr<DeviceProxyInterface> Client::NewDeviceProxy(
    const dbus::ObjectPath& device_path) {
  return std::make_unique<DeviceProxy>(bus_, device_path);
}

void Client::SetupDeviceProxy(const dbus::ObjectPath& device_path) {
  auto proxy = NewDeviceProxy(device_path);
  auto* ptr = proxy.get();
  devices_.emplace(device_path.value(),
                   std::make_unique<DeviceWrapper>(bus_, std::move(proxy)));
  ptr->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&Client::OnDevicePropertyChange,
                          weak_factory_.GetWeakPtr(), device_path.value()),
      base::BindOnce(&Client::OnDevicePropertyChangeRegistration,
                     weak_factory_.GetWeakPtr(), device_path.value()));
}

std::unique_ptr<ServiceProxyInterface> Client::NewServiceProxy(
    const dbus::ObjectPath& service_path) {
  return std::make_unique<ServiceProxy>(bus_, service_path);
}

void Client::SetupSelectedServiceProxy(const dbus::ObjectPath& service_path,
                                       const dbus::ObjectPath& device_path) {
  const auto it = devices_.find(device_path.value());
  if (it == devices_.end()) {
    LOG(DFATAL) << "Cannot find device [" << device_path.value() << "]";
    return;
  }

  auto proxy = NewServiceProxy(service_path);
  auto* ptr = proxy.get();
  it->second->set_service_proxy(std::move(proxy));
  ptr->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&Client::OnServicePropertyChange,
                          weak_factory_.GetWeakPtr(), device_path.value()),
      base::BindOnce(&Client::OnServicePropertyChangeRegistration,
                     weak_factory_.GetWeakPtr(), device_path.value()));
}

void Client::RegisterOnAvailableCallback(
    base::OnceCallback<void(bool)> handler) {
  bus_->GetObjectProxy(kFlimflamServiceName,
                       dbus::ObjectPath(kFlimflamServicePath))
      ->WaitForServiceToBeAvailable(std::move(handler));
}

void Client::RegisterProcessChangedHandler(
    const base::RepeatingCallback<void(bool)>& handler) {
  process_handler_ = handler;
}

void Client::RegisterDefaultServiceChangedHandler(
    const DefaultServiceChangedHandler& handler) {
  default_service_handlers_.emplace_back(handler);
}

void Client::RegisterDefaultDeviceChangedHandler(
    const DeviceChangedHandler& handler) {
  // Provide the current default device to the new handler.
  Device* device = nullptr;
  const auto it = devices_.find(default_device_path_);
  if (it != devices_.end()) {
    device = it->second->device();
  }

  handler.Run(device);

  default_device_handlers_.emplace_back(handler);
}

void Client::RegisterDeviceChangedHandler(const DeviceChangedHandler& handler) {
  device_handlers_.emplace_back(handler);
}

void Client::RegisterDeviceAddedHandler(const DeviceChangedHandler& handler) {
  // Provide the current list of devices.
  for (const auto& kv : devices_) {
    handler.Run(kv.second->device());
  }

  device_added_handlers_.emplace_back(handler);
}

void Client::RegisterDeviceRemovedHandler(const DeviceChangedHandler& handler) {
  device_removed_handlers_.emplace_back(handler);
}

void Client::OnOwnerChange(const std::string& old_owner,
                           const std::string& new_owner) {
  // Avoid resetting client state when |old_owner| is empty as there might be
  // race between owner change callback and shill startup callback. See also
  // b/307671293.
  if (old_owner.empty()) {
    return;
  }

  ReleaseDefaultServiceProxy();
  for (const auto& device : devices_) {
    device.second->release_object_proxy();
  }
  devices_.clear();

  bool reset = !new_owner.empty();
  if (reset) {
    VLOG(2) << "Shill reset";
  } else {
    VLOG(2) << "Shill lost";
  }

  if (!process_handler_.is_null()) {
    process_handler_.Run(reset);
  }
}

void Client::OnManagerPropertyChangeRegistration(const std::string& interface,
                                                 const std::string& signal_name,
                                                 bool success) {
  if (!success) {
    LOG(ERROR) << "Unable to register for Manager change events " << " for "
               << signal_name << " on " << interface;
    return;
  }
  brillo::VariantDictionary properties;
  if (!manager_proxy_ || !manager_proxy_->GetProperties(&properties, nullptr)) {
    LOG(WARNING) << "Unable to get shill Manager properties, likely because "
                    "shill is unavailable";
    return;
  }

  for (const auto& prop : {kDevicesProperty, kDefaultServiceProperty}) {
    auto it = properties.find(prop);
    if (it != properties.end()) {
      OnManagerPropertyChange(prop, it->second);
    } else {
      LOG(ERROR) << "Cannot find Manager property [" << prop << "]";
    }
  }
}

void Client::OnManagerPropertyChange(const std::string& property_name,
                                     const brillo::Any& property_value) {
  if (property_name == kDefaultServiceProperty) {
    HandleDefaultServiceChanged(property_value);
    return;
  }

  if (property_name == kDevicesProperty) {
    HandleDevicesChanged(property_value);
    return;
  }
}

void Client::HandleDefaultServiceChanged(const brillo::Any& property_value) {
  dbus::ObjectPath cur_path,
      service_path = property_value.TryGet<dbus::ObjectPath>();
  if (default_service_proxy_) {
    cur_path = default_service_proxy_->GetObjectPath();
  }

  if (service_path != cur_path) {
    LOG(INFO) << "Default service changed from [" << cur_path.value()
              << "] to [" << service_path.value() << "]";
  }
  ReleaseDefaultServiceProxy();

  // If the service is disconnected, run the handlers here since the normal flow
  // of doing so on property callback registration won't run.
  if (!service_path.IsValid() || service_path.value() == "/") {
    for (auto& handler : default_service_handlers_) {
      handler.Run("");
    }
    VLOG(2) << "Default service device is removed";
    for (auto& handler : default_device_handlers_) {
      handler.Run(nullptr);
    }
    return;
  }

  SetupDefaultServiceProxy(service_path);
}

void Client::AddDevice(const dbus::ObjectPath& device_path) {
  const std::string& path = device_path.value();
  if (devices_.find(path) != devices_.end()) {
    return;
  }

  VLOG(2) << "Device [" << path << "] added";
  SetupDeviceProxy(device_path);
}

void Client::HandleDevicesChanged(const brillo::Any& property_value) {
  std::set<std::string> latest;
  for (const auto& path :
       property_value.TryGet<std::vector<dbus::ObjectPath>>()) {
    latest.emplace(path.value());
    AddDevice(path);
  }

  for (auto it = devices_.begin(); it != devices_.end();) {
    if (latest.find(it->first) == latest.end()) {
      VLOG(2) << "Device [" << it->first << "] removed";
      if (!it->second->device()->ifname.empty()) {
        // If the ifname is empty, it should not be exposed so we don't need to
        // invoke the removed callback.
        for (auto& handler : device_removed_handlers_) {
          handler.Run(it->second->device());
        }
      }
      it->second->release_object_proxy();
      it = devices_.erase(it);
    } else {
      ++it;
    }
  }
}

void Client::OnDefaultServicePropertyChangeRegistration(
    const std::string& interface,
    const std::string& signal_name,
    bool success) {
  if (!success) {
    std::string path;
    if (default_service_proxy_) {
      path = default_service_proxy_->GetObjectPath().value();
    }

    LOG(ERROR) << "Unable to register for Service [" << path
               << "] change events " << " for " << signal_name << " on "
               << interface;
    return;
  }

  if (!default_service_proxy_) {
    LOG(ERROR) << "No default service";
    return;
  }
  const std::string service_path =
      default_service_proxy_->GetObjectPath().value();
  brillo::VariantDictionary properties;
  if (!default_service_proxy_->GetProperties(&properties, nullptr)) {
    LOG(ERROR) << "Unable to get properties for the default service ["
               << service_path << "]";
    return;
  }

  // Notify that the default service has changed.
  const auto type =
      brillo::GetVariantValueOrDefault<std::string>(properties, kTypeProperty);
  for (auto& handler : default_service_handlers_) {
    handler.Run(type);
  }

  OnDefaultServicePropertyChange(
      kIsConnectedProperty,
      brillo::GetVariantValueOrDefault<bool>(properties, kIsConnectedProperty));
  OnDefaultServicePropertyChange(
      kDeviceProperty, brillo::GetVariantValueOrDefault<dbus::ObjectPath>(
                           properties, kDeviceProperty));
}

void Client::OnDefaultServicePropertyChange(const std::string& property_name,
                                            const brillo::Any& property_value) {
  if (property_name != kDeviceProperty) {
    return;
  }

  std::string path = property_value.TryGet<dbus::ObjectPath>().value();
  if (path == default_device_path_) {
    return;
  }

  VLOG(2) << "Default service device changed to [" << path << "]";
  default_device_path_ = path;

  // When there is no service, run the handlers with a nullptr to indicate this
  // condition.
  if (default_device_path_ == "" || default_device_path_ == "/") {
    for (auto& handler : default_device_handlers_) {
      handler.Run(nullptr);
    }
    return;
  }

  // We generally expect to already be aware of the default device unless it
  // happens to be a VPN. In the case of the latter, add and track it (this will
  // ultimately fire the same handler after reading all the properties.
  const auto& it = devices_.find(default_device_path_);
  if (it != devices_.end()) {
    for (auto& handler : default_device_handlers_) {
      handler.Run(it->second->device());
    }
  } else {
    AddDevice(dbus::ObjectPath(default_device_path_));
  }
}

void Client::OnDevicePropertyChangeRegistration(const std::string& device_path,
                                                const std::string& interface,
                                                const std::string& signal_name,
                                                bool success) {
  if (!success) {
    LOG(ERROR) << "Unable to register for Device [" << device_path
               << "] change events " << " for " << signal_name << " on "
               << interface;
    return;
  }

  auto it = devices_.find(device_path);
  if (it == devices_.end()) {
    LOG(ERROR) << "Device [" << device_path << "] not found";
    return;
  }

  brillo::VariantDictionary properties;
  if (!it->second->proxy()->GetProperties(&properties, nullptr)) {
    LOG(ERROR) << "Unable to get properties for device [" << device_path << "]";
    return;
  }

  auto* device = it->second->device();
  device->type = ParseDeviceType(
      brillo::GetVariantValueOrDefault<std::string>(properties, kTypeProperty));
  if (device->type == Device::Type::kUnknown) {
    LOG(ERROR) << "Device [" << device_path << "] type is unknown";
  }

  if (device->type == Client::Device::Type::kCellular) {
    device->cellular_country_code = GetCellularProviderCountryCode(properties);
    device->cellular_primary_ifname =
        brillo::GetVariantValueOrDefault<std::string>(
            properties, kPrimaryMultiplexedInterfaceProperty);
  }

  const auto service_path = brillo::GetVariantValueOrDefault<dbus::ObjectPath>(
      properties, kSelectedServiceProperty);
  HandleSelectedServiceChanged(device_path, service_path, it->second.get());

  // OnDevicePropertyChange will triggers the callbacks. Calling this at the
  // last allows us to provide a Device struct populated with all the properties
  // available at the time.
  OnDevicePropertyChange(device_path, kInterfaceProperty,
                         properties[kInterfaceProperty]);
}

void Client::OnDevicePropertyChange(const std::string& device_path,
                                    const std::string& property_name,
                                    const brillo::Any& property_value) {
  auto it = devices_.find(device_path);
  if (it == devices_.end()) {
    LOG(ERROR) << "Device [" << device_path << "] not found";
    return;
  }

  Device* device = it->second->device();
  if (property_name == kInterfaceProperty) {
    HandleDeviceInterfaceChanged(device_path, property_value, device);
  } else if (property_name == kSelectedServiceProperty) {
    HandleSelectedServiceChanged(device_path, property_value, it->second.get());
  } else if (property_name == kHomeProviderProperty) {
    device->cellular_country_code = property_value.TryGet<
        std::map<std::string, std::string>>()[shill::kOperatorCountryKey];
  } else if (property_name == kPrimaryMultiplexedInterfaceProperty) {
    device->cellular_primary_ifname = property_value.TryGet<std::string>();
  } else {
    return;
  }

  // For a device without interface name, it should not be exposed so no
  // callback should be triggered.
  if (device->ifname.empty()) {
    return;
  }

  // If this is the default device then notify the handlers.
  if (device_path == default_device_path_) {
    for (auto& handler : default_device_handlers_) {
      handler.Run(device);
    }
  }

  // Notify the handlers interested in all device changes.
  for (auto& handler : device_handlers_) {
    handler.Run(device);
  }
}

void Client::HandleSelectedServiceChanged(const std::string& device_path,
                                          const brillo::Any& property_value,
                                          DeviceWrapper* device_wrapper) {
  auto* device = device_wrapper->device();

  auto service_path = property_value.TryGet<dbus::ObjectPath>();
  if (!service_path.IsValid() || service_path.value() == "/") {
    device->state = Device::ConnectionState::kUnknown;
    VLOG(2) << "Device [" << device_path << "] has no service";
    return;
  }

  SetupSelectedServiceProxy(service_path, dbus::ObjectPath(device_path));
  brillo::VariantDictionary properties;
  if (auto* proxy = device_wrapper->service_proxy()) {
    if (!proxy->GetProperties(&properties, nullptr)) {
      LOG(ERROR) << "Unable to get properties for device service ["
                 << service_path.value() << "]";
    }
  } else {
    LOG(DFATAL) << "Device [" << device_path
                << "] has no selected service proxy";
  }

  device->state =
      ParseConnectionState(brillo::GetVariantValueOrDefault<std::string>(
          properties, kStateProperty));
  if (device->state == Device::ConnectionState::kUnknown) {
    LOG(ERROR) << "Device [" << device_path << "] connection state for ["
               << service_path.value() << "] is unknown";
  }

  device->network_config =
      ParseNetworkConfigProperty(GetVariant<brillo::VariantDictionary>(
          properties, kNetworkConfigProperty));

  return;
}

void Client::HandleDeviceInterfaceChanged(const std::string& device_path,
                                          const brillo::Any& property_value,
                                          Device* device) {
  CHECK(device);
  std::string new_ifname = property_value.TryGet<std::string>();
  if (device->ifname.empty() && !new_ifname.empty()) {
    device->ifname = new_ifname;
    // Added callback should be called after we modify the |device|.
    for (auto& handler : device_added_handlers_) {
      handler.Run(device);
    }
  } else if (!device->ifname.empty() && new_ifname.empty()) {
    // Removed callback should be called before we modify the |device|.
    for (auto& handler : device_removed_handlers_) {
      handler.Run(device);
    }
    device->ifname = new_ifname;
  } else if (!device->ifname.empty() && !new_ifname.empty()) {
    // This should not happen. The interface name should go to empty before
    // change to another value.
    LOG(ERROR) << "Device [" << device_path << "] ifname changed from "
               << device->ifname << " to " << new_ifname;
  }
  // Both empty is expected when OnDevicePropertyChange is called at the end of
  // OnDevicePropertyChangeRegistration when the device has no interface yet.
}

void Client::OnServicePropertyChangeRegistration(const std::string& device_path,
                                                 const std::string& interface,
                                                 const std::string& signal_name,
                                                 bool success) {
  if (!success) {
    LOG(ERROR) << "Unable to register for Device [" << device_path
               << "] connected service change events " << " for " << signal_name
               << " on " << interface;
    return;
  }

  // This is OK for now since this signal handler is only used for device
  // connected services. If this changes in the future, then we need to
  // accommodate device_path being empty.
  const auto it = devices_.find(device_path);
  if (it == devices_.end()) {
    LOG(ERROR) << "Cannot find device [" << device_path << "]";
    return;
  }

  // This should really exist at this point...
  auto* service_proxy = it->second->service_proxy();
  if (!service_proxy) {
    LOG(DFATAL) << "Missing service proxy for device [" << device_path << "]";
    return;
  }

  brillo::VariantDictionary properties;
  if (!service_proxy->GetProperties(&properties, nullptr)) {
    LOG(ERROR) << "Unable to get connected service properties for device ["
               << device_path << "]";
    return;
  }

  OnServicePropertyChange(device_path, kStateProperty,
                          brillo::GetVariantValueOrDefault<std::string>(
                              properties, kStateProperty));
}

namespace {

// Updates connection state of |device| with |property_value|. Returns whether
// the value is changed.
bool ProcessStateChange(const std::string& device_path,
                        const brillo::Any& property_value,
                        Client::Device* device) {
  const auto state = ParseConnectionState(property_value.TryGet<std::string>());
  if (device->state == state) {
    return false;
  }

  if (IsConnectedState(device->state) || IsConnectedState(state)) {
    LOG(INFO) << "Device [" << device_path
              << "] connection state changed from [" << ToString(device->state)
              << "] to [" << ToString(state) << "]";
  }
  device->state = state;
  return true;
}

// Updates NetworkConfig of |device| with |property_value|. Returns whether
// the value is changed.
bool ProcessNetworkConfigChange(const std::string& device_path,
                                const brillo::Any& property_value,
                                Client::Device* device) {
  if (!property_value.IsTypeCompatible<brillo::VariantDictionary>()) {
    LOG(ERROR) << "Device [" << device_path
               << "] does not have a valid NetworkConfig value";
    return false;
  }

  const auto old_value = device->network_config;
  device->network_config = ParseNetworkConfigProperty(
      property_value.Get<brillo::VariantDictionary>());
  return device->network_config != old_value;
}

}  // namespace

void Client::OnServicePropertyChange(const std::string& device_path,
                                     const std::string& property_name,
                                     const brillo::Any& property_value) {
  if (property_name != kStateProperty &&
      property_name != kNetworkConfigProperty) {
    return;
  }

  const auto it = devices_.find(device_path);
  if (it == devices_.end()) {
    LOG(ERROR) << "Cannot find device [" << device_path << "]";
    return;
  }

  auto* device = it->second->device();
  bool has_change = false;
  if (property_name == kStateProperty) {
    has_change = ProcessStateChange(device_path, property_value, device);
  } else if (property_name == kNetworkConfigProperty) {
    has_change =
        ProcessNetworkConfigChange(device_path, property_value, device);
  } else {
    NOTREACHED();
  }

  if (!has_change) {
    return;
  }

  for (auto& handler : device_handlers_) {
    handler.Run(device);
  }

  if (device_path == default_device_path_) {
    for (auto& handler : default_device_handlers_) {
      handler.Run(device);
    }
  }
}

std::vector<std::unique_ptr<Client::Device>> Client::GetDevices() const {
  std::vector<std::unique_ptr<Client::Device>> devices;
  // Provide the devices with an interface name.
  for (const auto& [_, dev] : devices_) {
    if (dev->device()->ifname.empty()) {
      continue;
    }
    auto device = std::make_unique<Device>();
    device->type = dev->device()->type;
    device->ifname = dev->device()->ifname;
    device->state = dev->device()->state;
    device->network_config = dev->device()->network_config;
    device->cellular_country_code = dev->device()->cellular_country_code;
    device->cellular_primary_ifname = dev->device()->cellular_primary_ifname;
    devices.emplace_back(std::move(device));
  }
  return devices;
}

std::unique_ptr<Client::ManagerPropertyAccessor> Client::ManagerProperties(
    const base::TimeDelta& timeout) const {
  return std::make_unique<PropertyAccessor<ManagerProxyInterface>>(
      manager_proxy_.get(), timeout);
}

std::unique_ptr<Client::ServicePropertyAccessor>
Client::DefaultServicePropertyAccessor(const base::TimeDelta& timeout) const {
  if (default_service_proxy_.get() == nullptr) {
    LOG(ERROR) << "Failed to create property accessor because "
                  "there is no default service.";
    return nullptr;
  }
  return std::make_unique<PropertyAccessor<ServiceProxyInterface>>(
      default_service_proxy_.get(), timeout);
}

std::unique_ptr<brillo::VariantDictionary> Client::GetDefaultServiceProperties(
    const base::TimeDelta& timeout) const {
  brillo::ErrorPtr error;
  brillo::VariantDictionary properties;

  auto property_accessor = DefaultServicePropertyAccessor(timeout);
  if (!property_accessor) {
    return nullptr;
  }

  if (!property_accessor->Get(&properties, &error)) {
    LOG(ERROR) << "Failed to obtain default service properties: "
               << error->GetMessage();
    return nullptr;
  }

  return std::make_unique<brillo::VariantDictionary>(std::move(properties));
}

std::unique_ptr<Client::Device> Client::DefaultDevice(bool exclude_vpn) {
  brillo::ErrorPtr error;
  brillo::VariantDictionary properties;
  if (!manager_proxy_->GetProperties(&properties, &error)) {
    LOG(ERROR) << "Failed to obtain manager properties";
    return nullptr;
  }
  auto services =
      brillo::GetVariantValueOrDefault<std::vector<dbus::ObjectPath>>(
          properties, shill::kServicesProperty);

  dbus::ObjectPath device_path;
  shill::Client::Device::ConnectionState conn_state;
  NetworkConfig network_config;
  for (const auto& s : services) {
    properties.clear();
    if (!NewServiceProxy(s)->GetProperties(&properties, &error)) {
      LOG(ERROR) << "Failed to obtain service [" << s.value()
                 << "] properties: " << error->GetMessage();
      return nullptr;
    }
    if (exclude_vpn) {
      auto type = brillo::GetVariantValueOrDefault<std::string>(properties,
                                                                kTypeProperty);
      if (type.empty()) {
        LOG(ERROR) << "Failed to obtain property [" << shill::kTypeProperty
                   << "] on service [" << s.value() << "]";
        return nullptr;
      }
      if (type == kTypeVPN) {
        continue;
      }
    }

    conn_state =
        ParseConnectionState(brillo::GetVariantValueOrDefault<std::string>(
            properties, kStateProperty));
    network_config =
        ParseNetworkConfigProperty(GetVariant<brillo::VariantDictionary>(
            properties, kNetworkConfigProperty));
    device_path = brillo::GetVariantValueOrDefault<dbus::ObjectPath>(
        properties, kDeviceProperty);
    if (device_path.IsValid()) {
      break;
    }

    LOG(WARNING) << "Failed to obtain device for service [" << s.value() << "]";
    continue;
  }
  if (!device_path.IsValid()) {
    LOG(ERROR) << "No devices found";
    return nullptr;
  }

  auto proxy = NewDeviceProxy(device_path);
  properties.clear();
  if (!proxy->GetProperties(&properties, &error)) {
    LOG(ERROR) << "Failed to obtain properties for device ["
               << device_path.value() << "]: " << error->GetMessage();
    return nullptr;
  }
  auto device = std::make_unique<Device>();
  device->type = ParseDeviceType(
      brillo::GetVariantValueOrDefault<std::string>(properties, kTypeProperty));
  device->ifname = brillo::GetVariantValueOrDefault<std::string>(
      properties, kInterfaceProperty);
  device->state = conn_state;
  device->network_config = network_config;
  if (device->type == Client::Device::Type::kCellular) {
    device->cellular_country_code = GetCellularProviderCountryCode(properties);
    device->cellular_primary_ifname =
        brillo::GetVariantValueOrDefault<std::string>(
            properties, kPrimaryMultiplexedInterfaceProperty);
  }
  return device;
}

org::chromium::flimflam::ManagerProxyInterface* Client::GetManagerProxy()
    const {
  return manager_proxy_.get();
}

}  // namespace shill
