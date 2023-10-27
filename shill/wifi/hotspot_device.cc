// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/hotspot_device.h"

#include <memory>
#include <utility>

#include <base/containers/contains.h>
#include <base/functional/bind.h>

#include "shill/control_interface.h"
#include "shill/device.h"
#include "shill/event_dispatcher.h"
#include "shill/mac_address.h"
#include "shill/manager.h"
#include "shill/supplicant/supplicant_interface_proxy_interface.h"
#include "shill/supplicant/supplicant_process_proxy_interface.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/hotspot_service.h"
#include "shill/wifi/local_service.h"

namespace shill {

namespace {
const char kInterfaceStateUnknown[] = "unknown";
}  // namespace

// Constructor function
HotspotDevice::HotspotDevice(Manager* manager,
                             const std::string& primary_link_name,
                             const std::string& link_name,
                             const std::string& mac_address,
                             uint32_t phy_index,
                             LocalDevice::EventCallback callback)
    : LocalDevice(manager, IfaceType::kAP, link_name, phy_index, callback),
      primary_link_name_(primary_link_name),
      prev_primary_iface_control_state_(false),
      pending_phy_info_(false),
      service_(nullptr),
      supplicant_state_(kInterfaceStateUnknown),
      mac_address_(mac_address) {
  supplicant_primary_interface_path_ = RpcIdentifier("");
  supplicant_interface_proxy_.reset();
  supplicant_interface_path_ = RpcIdentifier("");
  supplicant_network_path_ = RpcIdentifier("");
}

HotspotDevice::~HotspotDevice() {}

bool HotspotDevice::Start() {
  auto wifi_phy = manager()->wifi_provider()->GetPhyAtIndex(phy_index());
  CHECK(wifi_phy);

  prev_primary_iface_control_state_ = SupplicantProcessProxy()->GetInterface(
      primary_link_name_, &supplicant_primary_interface_path_);

  if (!prev_primary_iface_control_state_) {
    // Connect wpa_supplicant to the primary interface.
    KeyValueStore create_interface_args;
    create_interface_args.Set<std::string>(
        WPASupplicant::kInterfacePropertyName, primary_link_name_);
    create_interface_args.Set<std::string>(
        WPASupplicant::kInterfacePropertyDriver, WPASupplicant::kDriverNL80211);
    create_interface_args.Set<std::string>(
        WPASupplicant::kInterfacePropertyConfigFile,
        WPASupplicant::kSupplicantConfPath);
    if (!SupplicantProcessProxy()->CreateInterface(
            create_interface_args, &supplicant_primary_interface_path_)) {
      LOG(ERROR) << __func__ << ": Cannot connect to the primary interface "
                 << primary_link_name_;
      return false;
    }
  }

  // Create Soft AP interface.
  if (!CreateInterface()) {
    return false;
  }

  pending_phy_info_ = true;
  if (!wifi_phy->reg_self_managed()) {
    // For non-self-managed solution, update the region domain and refresh Phy
    // info.
    manager()->wifi_provider()->UpdateRegAndPhyInfo(
        base::BindOnce(&HotspotDevice::OnPhyInfoReady, base::Unretained(this)));
  } else {
    // For self-managed solution, region information is lost when the STA
    // interface is tore down. Schedule a scan to fetch the region domain and
    // update Phy info.
    // TODO(b/291790575): Omited this scan on future DBS capable devices where
    // the STA interface is unaffected.
    Dispatcher()->PostTask(FROM_HERE,
                           base::BindOnce(&HotspotDevice::ScanTask,
                                          weak_ptr_factory_.GetWeakPtr()));
  }

  return true;
}

bool HotspotDevice::Stop() {
  bool ret = RemoveInterface();

  if (!prev_primary_iface_control_state_ &&
      !supplicant_primary_interface_path_.value().empty()) {
    // Disconnect wpa_supplicant from the primary interface.
    if (!SupplicantProcessProxy()->RemoveInterface(
            supplicant_primary_interface_path_)) {
      ret = false;
    }
  }
  supplicant_primary_interface_path_ = RpcIdentifier("");

  auto wifi_phy = manager()->wifi_provider()->GetPhyAtIndex(phy_index());
  if (wifi_phy == nullptr) {
    ret = false;
  } else if (!wifi_phy->reg_self_managed()) {
    // For non-self-managed solution, reset the region domain to world.
    manager()->wifi_provider()->ResetRegDomain();
  }

  pending_phy_info_ = false;
  return ret;
}

bool HotspotDevice::ConfigureService(std::unique_ptr<HotspotService> service) {
  CHECK(service);

  if (service_) {
    // Device has service configured.
    LOG(ERROR) << __func__
               << ": Configure service to device which already has a service "
                  "configured.";
    return false;
  }

  KeyValueStore service_params =
      service->GetSupplicantConfigurationParameters();
  if (!supplicant_interface_proxy_->AddNetwork(service_params,
                                               &supplicant_network_path_)) {
    LOG(ERROR) << __func__ << ": Failed to add network.";
    return false;
  }
  CHECK(!supplicant_network_path_.value().empty());

  service_ = std::move(service);
  service_->SetState(LocalService::LocalServiceState::kStateStarting);
  supplicant_interface_proxy_->SelectNetwork(supplicant_network_path_);
  return true;
}

bool HotspotDevice::DeconfigureService() {
  bool ret = true;

  if (!supplicant_network_path_.value().empty() &&
      !supplicant_interface_proxy_->RemoveNetwork(supplicant_network_path_)) {
    ret = false;
  }
  supplicant_network_path_ = RpcIdentifier("");

  if (service_) {
    service_->SetState(LocalService::LocalServiceState::kStateIdle);
    service_ = nullptr;
  }

  return ret;
}

bool HotspotDevice::CreateInterface() {
  CHECK(link_name());
  if (supplicant_interface_proxy_) {
    return true;
  }

  KeyValueStore create_interface_args;
  create_interface_args.Set<std::string>(WPASupplicant::kInterfacePropertyName,
                                         *link_name());
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
  if (!mac_address().empty()) {
    create_interface_args.Set<std::string>(
        WPASupplicant::kInterfacePropertyAddress, mac_address());
  }

  if (!SupplicantProcessProxy()->CreateInterface(create_interface_args,
                                                 &supplicant_interface_path_)) {
    // Interface might've already been created, attempt to retrieve it.
    if (!SupplicantProcessProxy()->GetInterface(*link_name(),
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
  CHECK(link_name());
  if (supplicant_state_ == new_state)
    return;

  LOG(INFO) << "Interface " << *link_name() << " state changed from "
            << supplicant_state_ << " to " << new_state;

  // Convert state change to corresponding device event.
  if (new_state == WPASupplicant::kInterfaceStateInterfaceDisabled) {
    PostDeviceEvent(DeviceEvent::kInterfaceDisabled);
  } else if (service_ && new_state == WPASupplicant::kInterfaceStateCompleted) {
    service_->SetState(LocalService::LocalServiceState::kStateUp);
  } else if (IsServiceUp() &&
             (new_state == WPASupplicant::kInterfaceStateDisconnected ||
              new_state == WPASupplicant::kInterfaceStateInactive)) {
    DeconfigureService();
  }

  supplicant_state_ = new_state;
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

void HotspotDevice::StationAdded(const RpcIdentifier& path,
                                 const KeyValueStore& properties) {
  CHECK(link_name());
  if (base::Contains(stations_, path)) {
    LOG(INFO) << "Receive StationAdded event for " << path.value()
              << ", which is already in the list. Ignore.";
    return;
  }

  stations_[path] = properties;

  auto aid = properties.Contains<uint16_t>(WPASupplicant::kStationPropertyAID)
                 ? properties.Get<uint16_t>(WPASupplicant::kStationPropertyAID)
                 : -1;
  LOG(INFO) << "Station [" << aid << "] connected to hotspot device "
            << *link_name() << ", total station count: " << stations_.size();
  PostDeviceEvent(DeviceEvent::kPeerConnected);
}

void HotspotDevice::StationRemoved(const RpcIdentifier& path) {
  CHECK(link_name());
  if (!base::Contains(stations_, path)) {
    LOG(INFO) << "Receive StationRemoved event for " << path.value()
              << ", which is not in the list. Ignore.";
    return;
  }

  auto aid =
      stations_[path].Contains<uint16_t>(WPASupplicant::kStationPropertyAID)
          ? stations_[path].Get<uint16_t>(WPASupplicant::kStationPropertyAID)
          : -1;
  stations_.erase(path);
  LOG(INFO) << "Station [" << aid << "] disconnected from hotspot device "
            << *link_name() << ", total station count: " << stations_.size();
  PostDeviceEvent(DeviceEvent::kPeerDisconnected);
}

std::vector<std::vector<uint8_t>> HotspotDevice::GetStations() {
  std::vector<std::vector<uint8_t>> stations;

  for (auto const& iter : stations_) {
    std::vector<uint8_t> station;
    if (iter.second.Contains<std::vector<uint8_t>>(
            WPASupplicant::kStationPropertyAddress)) {
      station = iter.second.Get<std::vector<uint8_t>>(
          WPASupplicant::kStationPropertyAddress);
    }
    stations.push_back(station);
  }

  return stations;
}

void HotspotDevice::OnPhyInfoReady() {
  pending_phy_info_ = false;
  // Phy information is up to date. Post interface enabled event.
  PostDeviceEvent(DeviceEvent::kInterfaceEnabled);
}

void HotspotDevice::ScanTask() {
  CHECK(link_name());
  LOG(INFO) << "Interface " << *link_name() << " scan requested.";
  if (!supplicant_interface_proxy_.get()) {
    LOG(ERROR) << "Ignoring scan request while supplicant does not control the "
                  "interface.";
    ScanDone(false);
    return;
  }

  KeyValueStore scan_args;
  scan_args.Set<std::string>(WPASupplicant::kPropertyScanType,
                             WPASupplicant::kScanTypeActive);
  scan_args.Set<bool>(WPASupplicant::kPropertyScanAllowRoam, false);

  if (!supplicant_interface_proxy_->Scan(scan_args)) {
    LOG(WARNING) << "Scan failed";
    ScanDone(false);
  }
}

void HotspotDevice::ScanDone(const bool& success) {
  CHECK(link_name());
  LOG(INFO) << "Interface " << *link_name() << " scan done. Scan "
            << (success ? "success" : "failed");

  if (pending_phy_info_) {
    // No matter success or not, require a PHY info update to be in sync with
    // the PHY.
    manager()->wifi_provider()->UpdatePhyInfo(
        base::BindOnce(&HotspotDevice::OnPhyInfoReady, base::Unretained(this)));
  }
}

}  // namespace shill
