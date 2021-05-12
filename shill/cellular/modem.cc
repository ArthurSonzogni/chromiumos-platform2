// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem.h"

#include <limits>
#include <tuple>

#include <base/bind.h>
#include <base/strings/stringprintf.h>

#include <ModemManager/ModemManager.h>

#include "shill/cellular/cellular.h"
#include "shill/control_interface.h"
#include "shill/dbus/dbus_properties_proxy.h"
#include "shill/device_info.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/net/rtnl_handler.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kModem;
static std::string ObjectID(const Modem* m) {
  return m->path().value().c_str();
}
}  // namespace Logging

// statics
constexpr char Modem::kFakeDevNameFormat[];
const char Modem::kFakeDevAddress[] = "000000000000";
const int Modem::kFakeDevInterfaceIndex = -1;
size_t Modem::fake_dev_serial_ = 0;

Modem::Modem(const std::string& service,
             const RpcIdentifier& path,
             DeviceInfo* device_info)
    : service_(service),
      path_(path),
      device_info_(device_info),
      type_(Cellular::kTypeInvalid),
      has_pending_device_info_(false),
      rtnl_handler_(RTNLHandler::GetInstance()) {
  SLOG(this, 1) << "Modem() Path: " << path.value();
}

Modem::~Modem() {
  SLOG(this, 1) << "~Modem() Path: " << path_.value();
  if (!device_) {
    return;
  }

  // Note: The Cellular Device |device_| is owned by DeviceInfo. It will not
  // be destroyed here, instead it will be kept around until/unless an RTNL
  // link delete message is received. If/when a new Modem instance is
  // constructed (e.g. after modemmanager restarts), the call to
  // DeviceInfo::GetCellularDevice will return the existing device for the
  // interface.
  device_->OnModemDestroyed();
}

void Modem::CreateDevice(const InterfaceToProperties& properties) {
  SLOG(this, 1) << __func__;
  dbus_properties_proxy_ =
      device_info_->manager()->control_interface()->CreateDBusPropertiesProxy(
          path(), service());
  dbus_properties_proxy_->SetModemManagerPropertiesChangedCallback(base::Bind(
      &Modem::OnModemManagerPropertiesChanged, base::Unretained(this)));
  dbus_properties_proxy_->SetPropertiesChangedCallback(
      base::Bind(&Modem::OnPropertiesChanged, base::Unretained(this)));

  uint32_t capabilities = std::numeric_limits<uint32_t>::max();
  InterfaceToProperties::const_iterator it =
      properties.find(MM_DBUS_INTERFACE_MODEM);
  if (it == properties.end()) {
    LOG(ERROR) << "Cellular device with no modem properties";
    return;
  }
  const KeyValueStore& modem_props = it->second;
  if (modem_props.Contains<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES)) {
    capabilities =
        modem_props.Get<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES);
  }

  if (capabilities & (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE)) {
    type_ = Cellular::kType3gpp;
  } else if (capabilities & MM_MODEM_CAPABILITY_CDMA_EVDO) {
    type_ = Cellular::kTypeCdma;
  } else {
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
  if (has_pending_device_info_ && link_name_ == link_name) {
    // has_pending_device_info_ is only set if we've already been through
    // CreateDeviceFromModemProperties() and saved our initial
    // properties already
    has_pending_device_info_ = false;
    CreateDeviceFromModemProperties(initial_properties_);
  }
}

std::string Modem::GetModemInterface() const {
  return std::string(MM_DBUS_INTERFACE_MODEM);
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

void Modem::CreateDeviceFromModemProperties(
    const InterfaceToProperties& properties) {
  if (device_)
    return;

  SLOG(this, 1) << __func__;

  InterfaceToProperties::const_iterator properties_it =
      properties.find(GetModemInterface());
  if (properties_it == properties.end()) {
    LOG(ERROR) << "Unable to find modem interface properties.";
    return;
  }

  std::string mac_address;
  int interface_index = -1;
  if (GetLinkName(properties_it->second, &link_name_)) {
    GetDeviceParams(&mac_address, &interface_index);
    if (interface_index < 0) {
      LOG(ERROR) << "Unable to create cellular device -- no interface index.";
      return;
    }
    if (mac_address.empty()) {
      // Save our properties, wait for OnDeviceInfoAvailable to be called.
      LOG(WARNING)
          << __func__
          << ": No hardware address, device creation pending device info.";
      initial_properties_ = properties;
      has_pending_device_info_ = true;
      return;
    }
    // Got the interface index and MAC address. Fall-through to actually
    // creating the Cellular object.
  } else {
    // Probably a PPP dongle.
    LOG(INFO) << "Cellular device without link name; assuming PPP dongle.";
    link_name_ = base::StringPrintf(kFakeDevNameFormat, fake_dev_serial_++);
    mac_address = kFakeDevAddress;
    interface_index = kFakeDevInterfaceIndex;
  }

  if (device_info_->IsDeviceBlocked(link_name_)) {
    LOG(INFO) << "Not creating cellular device for blocked interface "
              << link_name_ << ".";
    return;
  }

  device_ = device_info_->GetCellularDevice(interface_index, mac_address, this);

  // Give the device a chance to extract any capability-specific properties.
  for (properties_it = properties.begin(); properties_it != properties.end();
       ++properties_it) {
    device_->OnPropertiesChanged(properties_it->first, properties_it->second);
  }

  SLOG(this, 1) << "Cellular device created: " << device_->link_name()
                << " Enabled: " << device_->enabled();
}

bool Modem::GetDeviceParams(std::string* mac_address, int* interface_index) {
  // TODO(petkov): Get the interface index from DeviceInfo, similar to the MAC
  // address below.
  *interface_index = rtnl_handler_->GetInterfaceIndex(link_name_);
  if (*interface_index < 0) {
    return false;
  }

  ByteString address_bytes;
  if (!device_info_->GetMacAddress(*interface_index, &address_bytes)) {
    return false;
  }

  *mac_address = address_bytes.HexEncode();
  return true;
}

void Modem::OnPropertiesChanged(const std::string& interface,
                                const KeyValueStore& changed_properties) {
  SLOG(this, 3) << __func__;
  if (device_) {
    device_->OnPropertiesChanged(interface, changed_properties);
  }
}

void Modem::OnModemManagerPropertiesChanged(const std::string& interface,
                                            const KeyValueStore& properties) {
  SLOG(this, 3) << __func__;
  OnPropertiesChanged(interface, properties);
}

}  // namespace shill
