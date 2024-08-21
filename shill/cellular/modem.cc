// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem.h"

#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <chromeos/net-base/mac_address.h>
#include <ModemManager/ModemManager.h>

#include "shill/cellular/cellular.h"
#include "shill/control_interface.h"
#include "shill/device_info.h"
#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kModem;
static std::string ObjectID(const Modem* m) {
  return m->path().value().c_str();
}
}  // namespace Logging

// statics
size_t Modem::fake_dev_serial_ = 0;

Modem::Modem(const std::string& service,
             const RpcIdentifier& path,
             DeviceInfo* device_info)
    : service_(service), path_(path), device_info_(device_info) {
  SLOG(this, 1) << "Modem() Path: " << path.value();
}

Modem::~Modem() {
  SLOG(this, 1) << "~Modem() Path: " << path_.value();
  if (!interface_index_.has_value())
    return;

  CellularRefPtr cellular = GetExistingCellularDevice();
  if (cellular)
    cellular->OnModemDestroyed();
}

void Modem::CreateDevice(const InterfaceToProperties& properties) {
  SLOG(this, 1) << __func__;

  uint32_t capabilities = MM_MODEM_CAPABILITY_NONE;
  const auto iter = properties.find(MM_DBUS_INTERFACE_MODEM);
  if (iter == properties.end()) {
    LOG(ERROR) << "Cellular device with no modem properties";
    return;
  }
  const KeyValueStore& modem_props = iter->second;
  if (modem_props.Contains<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES)) {
    capabilities =
        modem_props.Get<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES);
  }

  if (!(capabilities & (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE |
                        MM_MODEM_CAPABILITY_5GNR))) {
    LOG(ERROR) << "Unsupported capabilities: " << capabilities;
    return;
  }

  // We cannot check the IP method to make sure it's not PPP. The IP
  // method will be checked later when the bearer object is fetched.
  CreateDeviceFromModemProperties(properties);
}

void Modem::OnDeviceInfoAvailable(const std::string& link_name) {
  SLOG(this, 1) << __func__ << ": " << link_name
                << " pending: " << has_pending_device_info_;
  if (link_name_ != link_name || !has_pending_device_info_)
    return;

  // has_pending_device_info_ is only set if we've already been through
  // CreateDeviceFromModemProperties() and saved our initial properties.
  has_pending_device_info_ = false;
  CreateDeviceFromModemProperties(initial_properties_);
}

bool Modem::GetLinkName(const KeyValueStore& modem_props,
                        std::string* name) const {
  if (!modem_props.ContainsVariant(MM_MODEM_PROPERTY_PORTS)) {
    LOG(ERROR) << "Device missing property: " << MM_MODEM_PROPERTY_PORTS;
    return false;
  }

  auto ports = modem_props.GetVariant(MM_MODEM_PROPERTY_PORTS)
                   .Get<std::vector<std::tuple<std::string, uint32_t>>>();
  std::string net_port;
  for (const auto& port_pair : ports) {
    if (std::get<1>(port_pair) == MM_MODEM_PORT_TYPE_NET) {
      net_port = std::get<0>(port_pair);
      break;
    }
  }

  if (net_port.empty()) {
    LOG(ERROR) << "Could not find net port used by the device.";
    return false;
  }

  *name = net_port;
  return true;
}

void Modem::CreateCellularDevice(DeviceInfo* device_info) {
  CellularRefPtr cellular;

  std::unique_ptr<brillo::CrosConfigInterface> cros_config =
      std::make_unique<brillo::CrosConfig>();

  std::string variant;
  if (!cros_config->GetString("/modem", "firmware-variant", &variant)) {
    LOG(INFO) << __func__
              << "Not creating cellular device for non-cellular variant.";
    return;
  }

  if (!device_info->manager()->ContainsIdentifier("device_cellular_store")) {
    LOG(INFO) << "Skipping device creation at startup to allow storage id "
                 "migration for variant: "
              << variant;
    return;
  }

  LOG(INFO) << "creating cellular device for variant" << variant;

  cellular = new Cellular(device_info->manager(), kCellularDeviceName,
                          kCellularDefaultInterfaceName, kFakeDevAddress,
                          kCellularDefaultInterfaceIndex,
                          modemmanager::kModemManager1ServiceName,
                          shill::RpcIdentifier());
  device_info->RegisterDevice(cellular);
}

void Modem::CreateDeviceFromModemProperties(
    const InterfaceToProperties& properties) {
  SLOG(this, 1) << __func__;

  const auto iter = properties.find(std::string(MM_DBUS_INTERFACE_MODEM));
  if (iter == properties.end()) {
    LOG(ERROR) << "Unable to find modem interface properties.";
    return;
  }
  const KeyValueStore& modem_props = iter->second;

  net_base::MacAddress mac_address;
  if (GetLinkName(modem_props, &link_name_)) {
    const std::optional<std::pair<int, net_base::MacAddress>> link_details =
        GetLinkDetailsFromDeviceInfo();
    if (!link_details.has_value()) {
      // Save our properties, wait for OnDeviceInfoAvailable to be called.
      LOG(WARNING) << "Delaying cellular device creation for interface "
                   << link_name_ << ".";
      initial_properties_ = properties;
      has_pending_device_info_ = true;
      return;
    }
    std::tie(interface_index_, mac_address) = *link_details;
    // Got the interface index and MAC address. Fall-through to actually
    // creating the Cellular object.
  } else {
    // Probably a PPP dongle.
    LOG(INFO) << "Cellular device without link name; assuming PPP dongle.";
    link_name_ = base::StringPrintf(kFakeDevNameFormat, fake_dev_serial_++);
    mac_address = kFakeDevAddress;
    interface_index_ = kFakeDevInterfaceIndex;
  }

  if (device_info_->IsDeviceBlocked(link_name_)) {
    LOG(INFO) << "Not creating cellular device for blocked interface "
              << link_name_ << ".";
    return;
  }

  CellularRefPtr device =
      GetOrCreateCellularDevice(interface_index_.value(), mac_address);
  device->SetInitialProperties(properties);

  SLOG(this, 1) << "Cellular device created: " << device->link_name()
                << " Enabled: " << device->enabled();
}

std::optional<std::pair<int, net_base::MacAddress>>
Modem::GetLinkDetailsFromDeviceInfo() {
  int interface_index = device_info_->GetIndex(link_name_);
  if (interface_index < 0) {
    return std::nullopt;
  }

  const std::optional<net_base::MacAddress> mac_address =
      device_info_->GetMacAddress(interface_index);
  if (!mac_address.has_value()) {
    return std::nullopt;
  }

  return std::make_pair(interface_index, *mac_address);
}

CellularRefPtr Modem::GetOrCreateCellularDevice(
    int interface_index, net_base::MacAddress mac_address) {
  LOG(INFO) << __func__ << "new interface index: " << interface_index
            << " new interface name: " << link_name_
            << " new MAC address: " << mac_address;

  CellularRefPtr cellular = GetExistingCellularDevice();

  if (cellular) {
    // Update the Cellular modem dbus path, mac address, interface index
    // and interface name to match the new Modem.
    cellular->UpdateModemProperties(path_, mac_address, interface_index,
                                    link_name_);
    return cellular;
  }

  // In regular cases we should always find the existing device above,
  // which was created during manager startup based on variant lookup.
  // We should reach here only if this is first boot with new storage id
  // or for cellular devices where variant is not configured correctly.
  cellular =
      new Cellular(device_info_->manager(), kCellularDeviceName, link_name_,
                   mac_address, interface_index, service_, path_);
  device_info_->RegisterDevice(cellular);
  return cellular;
}

CellularRefPtr Modem::GetExistingCellularDevice() const {
  DeviceRefPtr device = nullptr;
  device =
      device_info_->manager()->GetDeviceWithTechnology(Technology::kCellular);
  LOG(INFO) << __func__ << "device: " << device;
  if (!device)
    return nullptr;
  CHECK_EQ(device->technology(), Technology::kCellular);
  return static_cast<Cellular*>(device.get());
}

}  // namespace shill
