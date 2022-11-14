// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/hotspot_device.h"

#include <string.h>

#include <base/bind.h>

#include "shill/event_dispatcher.h"
#include "shill/control_interface.h"
#include "shill/store/property_store.h"
#include "shill/supplicant/supplicant_interface_proxy_interface.h"
#include "shill/supplicant/supplicant_process_proxy_interface.h"
#include "shill/supplicant/wpa_supplicant.h"

namespace shill {

namespace {
const char kInterfaceStateUnknown[] = "unknown";
}  // namespace

// Constructor function
HotspotDevice::HotspotDevice(Manager* manager,
                             const std::string& link_name,
                             const std::string& mac_address,
                             uint32_t phy_index,
                             LocalDevice::EventCallback callback)
    : LocalDevice(
          manager, IfaceType::kAP, link_name, mac_address, phy_index, callback),
      supplicant_state_(kInterfaceStateUnknown) {
  supplicant_interface_proxy_.reset();
  supplicant_interface_path_ = RpcIdentifier("");
}

HotspotDevice::~HotspotDevice() {}

bool HotspotDevice::Start() {
  return CreateInterface();
}

bool HotspotDevice::Stop() {
  return RemoveInterface();
}

bool HotspotDevice::CreateInterface() {
  if (supplicant_interface_proxy_) {
    return true;
  }

  KeyValueStore create_interface_args;
  create_interface_args.Set<std::string>(WPASupplicant::kInterfacePropertyName,
                                         link_name());
  create_interface_args.Set<std::string>(
      WPASupplicant::kInterfacePropertyDriver, WPASupplicant::kDriverNL80211);
  create_interface_args.Set<std::string>(
      WPASupplicant::kInterfacePropertyConfigFile,
      WPASupplicant::kSupplicantConfPath);
  create_interface_args.Set<bool>(WPASupplicant::kInterfacePropertyCreate,
                                  true);
  create_interface_args.Set<std::string>(
      WPASupplicant::kInterfacePropertyType,
      WPASupplicant::kInterfacePropertyTypeAP);

  if (!SupplicantProcessProxy()->CreateInterface(create_interface_args,
                                                 &supplicant_interface_path_)) {
    // Interface might've already been created, attempt to retrieve it.
    if (!SupplicantProcessProxy()->GetInterface(link_name(),
                                                &supplicant_interface_path_)) {
      LOG(ERROR) << __func__ << ": Failed to create interface with supplicant.";
      return false;
    }
  }

  supplicant_interface_proxy_ =
      ControlInterface()->CreateSupplicantInterfaceProxy(
          this, supplicant_interface_path_);
  return true;
}

bool HotspotDevice::RemoveInterface() {
  bool ret = true;
  supplicant_interface_proxy_.reset();
  if (!supplicant_interface_path_.value().empty() &&
      !SupplicantProcessProxy()->RemoveInterface(supplicant_interface_path_)) {
    ret = false;
  }
  supplicant_interface_path_ = RpcIdentifier("");
  return ret;
}

void HotspotDevice::StateChanged(const std::string& new_state) {
  if (supplicant_state_ == new_state)
    return;

  LOG(INFO) << "Interface " << link_name() << " state changed from "
            << supplicant_state_ << " to " << new_state;
  if (new_state == WPASupplicant::kInterfaceStateInterfaceDisabled)
    PostDeviceEvent(DeviceEvent::kInterfaceDisabled);

  supplicant_state_ = new_state;
  // TODO(b/235762279): emit kServiceUp and kServiceDown device events at
  // appropriate wpa_supplicant state.
}

void HotspotDevice::PropertiesChangedTask(const KeyValueStore& properties) {
  if (properties.Contains<std::string>(
          WPASupplicant::kInterfacePropertyState)) {
    StateChanged(
        properties.Get<std::string>(WPASupplicant::kInterfacePropertyState));
  }

  // TODO(b/235762161): Add Stations property changed event handler to emit
  // kPeerConnected and kPeerDisconnected device events.
}

// wpa_supplicant dbus event handlers for SupplicantEventDelegateInterface
void HotspotDevice::PropertiesChanged(const KeyValueStore& properties) {
  Dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(&HotspotDevice::PropertiesChangedTask,
                                weak_ptr_factory_.GetWeakPtr(), properties));
}

}  // namespace shill
