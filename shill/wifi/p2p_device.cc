// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_device.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/control_interface.h"
#include "shill/manager.h"
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
  supplicant_p2pdevice_proxy_.reset();
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
    LOG(WARNING) << log_name() << ": Tried to create group while in state "
                 << P2PDeviceStateName(state_);
    return false;
  }
  SetState(P2PDeviceState::kGOStarting);
  return SetService(std::move(service));
}

bool P2PDevice::Connect(std::unique_ptr<P2PService> service) {
  if (state_ != P2PDeviceState::kReady) {
    LOG(WARNING) << log_name() << ": Tried to connect while in state "
                 << P2PDeviceStateName(state_);
    return false;
  }
  SetState(P2PDeviceState::kClientAssociating);
  return SetService(std::move(service));
}

bool P2PDevice::RemoveGroup() {
  if (!InGOState()) {
    LOG(WARNING) << log_name() << ": Tried to remove a group while in state "
                 << P2PDeviceStateName(state_);
    return false;
  }
  SetState(P2PDeviceState::kGOStopping);
  DeleteService();
  return true;
}

bool P2PDevice::Disconnect() {
  if (!InClientState()) {
    LOG(WARNING) << log_name() << ": Tried to disconnect while in state "
                 << P2PDeviceStateName(state_);
    return false;
  }
  SetState(P2PDeviceState::kClientDisconnecting);
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

bool P2PDevice::SetService(std::unique_ptr<P2PService> service) {
  CHECK(service);
  if (service_) {
    // Device has service configured.
    LOG(ERROR) << log_name()
               << ": Attempted to set service on a device which "
                  "already has a service configured.";
    return false;
  }
  service_ = std::move(service);
  service_->SetState(LocalService::LocalServiceState::kStateStarting);
  return true;
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

bool P2PDevice::ConnectToSupplicantP2PDeviceProxy(
    const RpcIdentifier& interface) {
  if (supplicant_p2pdevice_proxy_) {
    LOG(WARNING) << log_name()
                 << ": Tried to connect to P2PDevice proxy while "
                    "already connected";
    return false;
  }
  supplicant_p2pdevice_proxy_ =
      ControlInterface()->CreateSupplicantP2PDeviceProxy(this, interface);
  if (!supplicant_p2pdevice_proxy_) {
    LOG(ERROR) << log_name()
               << ": Failed to connect to P2PDevice proxy, "
                  "path: "
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
  if (!ConnectToSupplicantP2PDeviceProxy(interface_path)) {
    return false;
  }
  return true;
}

void P2PDevice::TeardownGroup() {
  DisconnectFromSupplicantP2PDeviceProxy();
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

}  // namespace shill
