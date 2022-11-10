// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/hotspot_device.h"

#include "shill/control_interface.h"
#include "shill/supplicant/supplicant_interface_proxy_interface.h"
#include "shill/supplicant/supplicant_process_proxy_interface.h"
#include "shill/supplicant/wpa_supplicant.h"

namespace shill {
// Constructor function
HotspotDevice::HotspotDevice(Manager* manager,
                             const std::string& link_name,
                             const std::string& mac_address,
                             uint32_t phy_index,
                             LocalDevice::EventCallback callback)
    : LocalDevice(manager,
                  IfaceType::kAP,
                  link_name,
                  mac_address,
                  phy_index,
                  callback) {
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
  RpcIdentifier interface_path;
  supplicant_interface_proxy_.reset();
  if (!supplicant_interface_path_.value().empty()) {
    if (!SupplicantProcessProxy()->RemoveInterface(
            supplicant_interface_path_)) {
      ret = false;
    }
  }
  supplicant_interface_path_ = RpcIdentifier("");
  return ret;
}

// wpa_supplicant dbus event handlers for SupplicantEventDelegateInterface
void HotspotDevice::PropertiesChanged(const KeyValueStore& properties) {
  // TODO(b/235762279): Add State property changed event handler to emit
  // kServiceUp and kServiceDown device events.
  // TODO(b/235762161): Add Stations property changed event handler to emit
  // kPeerConnected and kPeerDisconnected device events.
}

}  // namespace shill
