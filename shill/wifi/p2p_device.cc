// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_device.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <chromeos/dbus/shill/dbus-constants.h>
#include <net-base/byte_utils.h>
#include <net-base/mac_address.h>

#include "shill/control_interface.h"
#include "shill/manager.h"
#include "shill/supplicant/supplicant_group_proxy_interface.h"
#include "shill/supplicant/supplicant_interface_proxy_interface.h"
#include "shill/supplicant/supplicant_p2pdevice_proxy_interface.h"
#include "shill/supplicant/wpa_supplicant.h"

namespace shill {

// Constructor function
P2PDevice::P2PDevice(Manager* manager,
                     LocalDevice::IfaceType iface_type,
                     const std::string& primary_link_name,
                     uint32_t phy_index,
                     uint32_t shill_id,
                     LocalDevice::EventCallback callback)
    : LocalDevice(manager, iface_type, std::nullopt, phy_index, callback),
      primary_link_name_(primary_link_name),
      shill_id_(shill_id),
      state_(P2PDeviceState::kUninitialized) {
  // A P2PDevice with a non-P2P interface type makes no sense.
  CHECK(iface_type == LocalDevice::IfaceType::kP2PGO ||
        iface_type == LocalDevice::IfaceType::kP2PClient);
  log_name_ = (iface_type == LocalDevice::IfaceType::kP2PGO)
                  ? "p2p_go_" + std::to_string(shill_id)
                  : "p2p_client_" + std::to_string(shill_id);
  supplicant_interface_proxy_.reset();
  supplicant_p2pdevice_proxy_.reset();
  supplicant_group_proxy_.reset();
  supplicant_persistent_group_path_ = RpcIdentifier("");
  LOG(INFO) << log_name() << ": P2PDevice created";
}

P2PDevice::~P2PDevice() {
  LOG(INFO) << log_name() << ": P2PDevice destroyed";
}

// static
const char* P2PDevice::P2PDeviceStateName(P2PDeviceState state) {
  switch (state) {
    case P2PDeviceState::kUninitialized:
      return kP2PDeviceStateUninitialized;
    case P2PDeviceState::kReady:
      return kP2PDeviceStateReady;
    case P2PDeviceState::kClientAssociating:
      return kP2PDeviceStateClientAssociating;
    case P2PDeviceState::kClientConfiguring:
      return kP2PDeviceStateClientConfiguring;
    case P2PDeviceState::kClientConnected:
      return kP2PDeviceStateClientConnected;
    case P2PDeviceState::kClientDisconnecting:
      return kP2PDeviceStateClientDisconnecting;
    case P2PDeviceState::kGOStarting:
      return kP2PDeviceStateGOStarting;
    case P2PDeviceState::kGOConfiguring:
      return kP2PDeviceStateGOConfiguring;
    case P2PDeviceState::kGOActive:
      return kP2PDeviceStateGOActive;
    case P2PDeviceState::kGOStopping:
      return kP2PDeviceStateGOStopping;
  }
  NOTREACHED() << "Unhandled P2P state " << static_cast<int>(state);
  return "Invalid";
}

KeyValueStore P2PDevice::GetGroupInfo() const {
  // TODO(b/301049348): integration with supplicant D-Bus is required
  // to provide properties of active p2p group.
  KeyValueStore groupInfo;
  groupInfo.Set<Integer>(kP2PGroupInfoShillIDProperty, shill_id());
  groupInfo.Set<String>(kP2PGroupInfoStateProperty, kP2PGroupInfoStateIdle);
  return groupInfo;
}

KeyValueStore P2PDevice::GetClientInfo() const {
  // TODO(b/301049348): integration with supplicant D-Bus is required
  // to provide properties of connected p2p client.
  KeyValueStore clientInfo;
  clientInfo.Set<Integer>(kP2PClientInfoShillIDProperty, shill_id());
  clientInfo.Set<String>(kP2PClientInfoStateProperty, kP2PClientInfoStateIdle);
  return clientInfo;
}

bool P2PDevice::Start() {
  SetState(P2PDeviceState::kReady);
  return true;
}

bool P2PDevice::Stop() {
  bool ret = true;
  if (InClientState()) {
    if (!Disconnect()) {
      ret = false;
    }
  } else if (InGOState()) {
    if (!RemoveGroup()) {
      ret = false;
    }
  }
  SetState(P2PDeviceState::kUninitialized);
  return ret;
}

bool P2PDevice::CreateGroup(std::unique_ptr<P2PService> service) {
  if (state_ != P2PDeviceState::kReady) {
    LOG(ERROR) << log_name() << ": Tried to create group while in state "
               << P2PDeviceStateName(state_);
    return false;
  }
  CHECK(service);
  if (service_) {
    LOG(ERROR) << log_name()
               << ": Attempted to create group on a device which already has a "
                  "service configured.";
    return false;
  }
  KeyValueStore properties = service->GetSupplicantConfigurationParameters();
  if (!StartSupplicantGroupForGO(properties)) {
    return false;
  }
  SetService(std::move(service));
  SetState(P2PDeviceState::kGOStarting);
  // TODO(b/308081318): set service up on GroupStarted or NetworkStarted
  // service_->SetState(LocalService::LocalServiceState::kStateUp);
  return true;
}

bool P2PDevice::Connect(std::unique_ptr<P2PService> service) {
  if (state_ != P2PDeviceState::kReady) {
    LOG(ERROR) << log_name() << ": Tried to connect while in state "
               << P2PDeviceStateName(state_);
    return false;
  }
  CHECK(service);
  if (service_) {
    LOG(ERROR) << log_name()
               << ": Attempted to connect to group on a device which already "
                  "has a service configured.";
    return false;
  }
  KeyValueStore properties = service->GetSupplicantConfigurationParameters();
  if (!StartSupplicantGroupForClient(properties)) {
    return false;
  }
  SetService(std::move(service));
  SetState(P2PDeviceState::kClientAssociating);
  // TODO(b/308081318): set service up on GroupStarted or NetworkStarted
  // service_->SetState(LocalService::LocalServiceState::kStateUp);
  return true;
}

bool P2PDevice::RemoveGroup() {
  if (!InGOState()) {
    LOG(WARNING) << log_name() << ": Tried to remove a group while in state "
                 << P2PDeviceStateName(state_);
    return false;
  }
  FinishSupplicantGroup();
  SetState(P2PDeviceState::kGOStopping);
  // TODO(b/308081318): delete service on GroupFinished
  DeleteService();
  return true;
}

bool P2PDevice::Disconnect() {
  if (!InClientState()) {
    LOG(WARNING) << log_name() << ": Tried to disconnect while in state "
                 << P2PDeviceStateName(state_);
    return false;
  }
  FinishSupplicantGroup();
  SetState(P2PDeviceState::kClientDisconnecting);
  // TODO(b/308081318): delete service on GroupFinished
  DeleteService();
  return true;
}

bool P2PDevice::InGOState() const {
  return (state_ == P2PDeviceState::kGOStarting ||
          state_ == P2PDeviceState::kGOConfiguring ||
          state_ == P2PDeviceState::kGOActive ||
          state_ == P2PDeviceState::kGOStopping);
}

bool P2PDevice::InClientState() const {
  return (state_ == P2PDeviceState::kClientAssociating ||
          state_ == P2PDeviceState::kClientConfiguring ||
          state_ == P2PDeviceState::kClientConnected ||
          state_ == P2PDeviceState::kClientDisconnecting);
}

SupplicantP2PDeviceProxyInterface* P2PDevice::SupplicantPrimaryP2PDeviceProxy()
    const {
  return manager()
      ->wifi_provider()
      ->p2p_manager()
      ->SupplicantPrimaryP2PDeviceProxy();
}

bool P2PDevice::StartSupplicantGroupForGO(const KeyValueStore& properties) {
  if (!SupplicantPrimaryP2PDeviceProxy()) {
    LOG(ERROR) << log_name()
               << ": Tried to start group while the primary P2PDevice proxy is "
                  "not connected";
    return false;
  }
  if (!SupplicantPrimaryP2PDeviceProxy()->GroupAdd(properties)) {
    LOG(ERROR) << log_name()
               << ": Failed to GroupAdd via the primary P2PDevice proxy";
    return false;
  }
  return true;
}

bool P2PDevice::StartSupplicantGroupForClient(const KeyValueStore& properties) {
  if (!SupplicantPrimaryP2PDeviceProxy()) {
    LOG(WARNING) << log_name()
                 << ": Tried to join group while the primary "
                    "P2PDevice proxy is not connected";
    return false;
  }
  // Right now, there are no commands available in wpa_supplicant to bypass
  // P2P discovery and join an existing P2P group directly. Instead `GroupAdd`
  // with persistent group object path and role specified as client can be used
  // to join the P2P network. For client mode, even if group is specified as
  // persistent, it will still follow the GO's lead and join as a non-persistent
  // group. For GO mode, the `GroupAdd` is used directly so that it creates
  // a non-persistent group.
  if (!SupplicantPrimaryP2PDeviceProxy()->AddPersistentGroup(
          properties, &supplicant_persistent_group_path_)) {
    LOG(ERROR) << log_name()
               << ": Failed to AddPersistentGroup via the primary"
                  " P2PDevice proxy";
    return false;
  }
  if (supplicant_persistent_group_path_.value().empty()) {
    LOG(ERROR) << log_name()
               << ": Got empty persistent group path from "
                  "the primary P2PDevice proxy";
    return false;
  }
  KeyValueStore p2pgroup_args;
  p2pgroup_args.Set<RpcIdentifier>(
      WPASupplicant::kGroupAddPropertyPersistentPath,
      supplicant_persistent_group_path_);
  if (!SupplicantPrimaryP2PDeviceProxy()->GroupAdd(p2pgroup_args)) {
    LOG(ERROR) << log_name()
               << ": Failed to GroupAdd via the primary "
                  "P2PDevice proxy";
    SupplicantPrimaryP2PDeviceProxy()->RemovePersistentGroup(
        supplicant_persistent_group_path_);
    supplicant_persistent_group_path_ = RpcIdentifier("");
    return false;
  }
  return true;
}

bool P2PDevice::FinishSupplicantGroup() {
  if (!supplicant_p2pdevice_proxy_) {
    LOG(ERROR)
        << log_name()
        << ": Tried to stop group while P2PDevice proxy is not connected";
    return false;
  }
  if (!supplicant_p2pdevice_proxy_->Disconnect()) {
    LOG(ERROR) << log_name() << ": Failed to Disconnect via P2PDevice proxy";
    return false;
  }
  return true;
}

void P2PDevice::SetService(std::unique_ptr<P2PService> service) {
  service_ = std::move(service);
  service_->SetState(LocalService::LocalServiceState::kStateStarting);
}

void P2PDevice::DeleteService() {
  if (!service_) {
    return;
  }
  service_->SetState(LocalService::LocalServiceState::kStateIdle);
  service_ = nullptr;
}

void P2PDevice::SetState(P2PDeviceState state) {
  if (state_ == state)
    return;
  LOG(INFO) << log_name() << ": State changed: " << P2PDeviceStateName(state_)
            << " -> " << P2PDeviceStateName(state);
  state_ = state;
}

bool P2PDevice::ConnectToSupplicantInterfaceProxy(
    const RpcIdentifier& object_path) {
  if (supplicant_interface_proxy_) {
    LOG(WARNING) << log_name()
                 << ": Tried to connect to the Interface proxy while it is "
                    "already connected";
    return false;
  }
  supplicant_interface_proxy_ =
      ControlInterface()->CreateSupplicantInterfaceProxy(this, object_path);
  if (!supplicant_interface_proxy_) {
    LOG(ERROR) << log_name()
               << ": Failed to connect to the Interface proxy, path: "
               << object_path.value();
    return false;
  }
  LOG(INFO) << log_name()
            << ": Interface proxy connected, path: " << object_path.value();
  return true;
}

void P2PDevice::DisconnectFromSupplicantInterfaceProxy() {
  if (supplicant_interface_proxy_) {
    supplicant_interface_proxy_.reset();
    LOG(INFO) << log_name() << ": Interface proxy disconnected";
  }
}

String P2PDevice::GetInterfaceName() const {
  String ifname;
  if (!supplicant_interface_proxy_->GetIfname(&ifname)) {
    LOG(ERROR) << log_name() << ": Failed to GetIfname via Interface proxy";
    return "";
  }
  return ifname;
}

bool P2PDevice::ConnectToSupplicantP2PDeviceProxy(
    const RpcIdentifier& interface) {
  if (supplicant_p2pdevice_proxy_) {
    LOG(WARNING)
        << log_name()
        << ": Tried to connect to P2PDevice proxy while already connected";
    return false;
  }
  supplicant_p2pdevice_proxy_ =
      ControlInterface()->CreateSupplicantP2PDeviceProxy(this, interface);
  if (!supplicant_p2pdevice_proxy_) {
    LOG(ERROR) << log_name() << ": Failed to connect to P2PDevice proxy, path: "
               << interface.value();
    return false;
  }
  LOG(INFO) << log_name()
            << ": P2PDevice proxy connected, path: " << interface.value();
  return true;
}

void P2PDevice::DisconnectFromSupplicantP2PDeviceProxy() {
  if (supplicant_p2pdevice_proxy_) {
    supplicant_p2pdevice_proxy_.reset();
    LOG(INFO) << log_name() << ": P2PDevice proxy disconnected";
  }
}

bool P2PDevice::ConnectToSupplicantGroupProxy(const RpcIdentifier& group) {
  if (supplicant_group_proxy_) {
    LOG(WARNING) << log_name()
                 << ": Tried to connect to the Group proxy while it is already "
                    "connected";
    return false;
  }
  supplicant_group_proxy_ =
      ControlInterface()->CreateSupplicantGroupProxy(this, group);
  if (!supplicant_group_proxy_) {
    LOG(ERROR) << log_name() << ": Failed to connect to the Group proxy, path: "
               << group.value();
    return false;
  }
  LOG(INFO) << log_name() << ": Group proxy connected, path: " << group.value();
  return true;
}

void P2PDevice::DisconnectFromSupplicantGroupProxy(void) {
  if (supplicant_group_proxy_) {
    supplicant_group_proxy_.reset();
    LOG(INFO) << log_name() << ": Group proxy disconnected";
  }
}

String P2PDevice::GetGroupSSID() const {
  ByteArray ssid;
  if (!supplicant_group_proxy_->GetSSID(&ssid)) {
    LOG(ERROR) << log_name() << ": Failed to GetSSID via Group proxy";
    return "";
  }
  return net_base::byte_utils::ByteStringFromBytes(ssid);
}

String P2PDevice::GetGroupBSSID() const {
  ByteArray bssid;
  if (!supplicant_group_proxy_->GetBSSID(&bssid)) {
    LOG(ERROR) << log_name() << ": Failed to GetBSSID via Group proxy";
    return "";
  }
  return net_base::MacAddress::CreateFromBytes(bssid)->ToString();
}

Integer P2PDevice::GetGroupFrequency() const {
  uint16_t frequency = 0;
  if (!supplicant_group_proxy_->GetFrequency(&frequency)) {
    LOG(ERROR) << log_name() << ": Failed to GetFrequency via Group proxy";
    return 0;
  }
  return frequency;
}

String P2PDevice::GetGroupPassphrase() const {
  std::string passphrase;
  if (!supplicant_group_proxy_->GetPassphrase(&passphrase)) {
    LOG(ERROR) << log_name() << ": Failed to GetPassphrase via Group proxy";
    return "";
  }
  return passphrase;
}

bool P2PDevice::SetupGroup(const KeyValueStore& properties) {
  RpcIdentifier interface_path = RpcIdentifier("");
  if (properties.Contains<RpcIdentifier>(
          WPASupplicant::kGroupStartedPropertyInterfaceObject)) {
    interface_path = properties.Get<RpcIdentifier>(
        WPASupplicant::kGroupStartedPropertyInterfaceObject);
  }
  if (interface_path.value().empty()) {
    LOG(ERROR) << log_name() << ": Failed to " << __func__
               << " without interface path";
    return false;
  }
  RpcIdentifier group_path = RpcIdentifier("");
  if (properties.Contains<RpcIdentifier>(
          WPASupplicant::kGroupStartedPropertyGroupObject)) {
    group_path = properties.Get<RpcIdentifier>(
        WPASupplicant::kGroupStartedPropertyGroupObject);
  }
  if (group_path.value().empty()) {
    LOG(ERROR) << log_name() << ": Failed to " << __func__
               << " without group path";
    return false;
  }
  if (!ConnectToSupplicantInterfaceProxy(interface_path) ||
      !ConnectToSupplicantP2PDeviceProxy(interface_path) ||
      !ConnectToSupplicantGroupProxy(group_path)) {
    TeardownGroup();
    return false;
  }

  link_name_ = GetInterfaceName();
  if (!link_name_.value().empty())
    LOG(INFO) << log_name() << ": Link name configured: " << link_name_.value();

  group_ssid_ = GetGroupSSID();
  if (!group_ssid_.empty())
    LOG(INFO) << log_name() << ": SSID configured: " << group_ssid_;

  group_bssid_ = GetGroupBSSID();
  if (!group_bssid_.empty())
    LOG(INFO) << log_name() << ": BSSID configured: " << group_bssid_;

  group_frequency_ = GetGroupFrequency();
  if (group_frequency_)
    LOG(INFO) << log_name() << ": Freqency configured: " << group_frequency_;

  group_passphrase_ = GetGroupPassphrase();
  if (!group_passphrase_.empty())
    LOG(INFO) << log_name() << ": Passphrase configured: " << group_passphrase_;

  return true;
}

void P2PDevice::TeardownGroup() {
  DisconnectFromSupplicantGroupProxy();
  DisconnectFromSupplicantP2PDeviceProxy();
  DisconnectFromSupplicantInterfaceProxy();

  if (!supplicant_persistent_group_path_.value().empty()) {
    SupplicantPrimaryP2PDeviceProxy()->RemovePersistentGroup(
        supplicant_persistent_group_path_);
    supplicant_persistent_group_path_ = RpcIdentifier("");
  }
}

void P2PDevice::GroupStarted(const KeyValueStore& properties) {
  LOG(INFO) << log_name() << ": Got " << __func__ << " while in state "
            << P2PDeviceStateName(state_);
  SetupGroup(properties);
}

void P2PDevice::GroupFinished(const KeyValueStore& properties) {
  LOG(INFO) << log_name() << ": Got " << __func__ << " while in state "
            << P2PDeviceStateName(state_);
  TeardownGroup();
}

void P2PDevice::GroupFormationFailure(const std::string& reason) {
  LOG(WARNING) << log_name() << ": Got " << __func__ << " while in state "
               << P2PDeviceStateName(state_);
}

void P2PDevice::PeerJoined(const dbus::ObjectPath& peer) {
  LOG(INFO) << log_name() << ": Got " << __func__ << " while in state "
            << P2PDeviceStateName(state_);
}

void P2PDevice::PeerDisconnected(const dbus::ObjectPath& peer) {
  LOG(INFO) << log_name() << ": Got " << __func__ << " while in state "
            << P2PDeviceStateName(state_);
}

}  // namespace shill
