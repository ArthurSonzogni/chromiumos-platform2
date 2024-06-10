// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_device.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/rtnl_handler.h>
#include <chromeos/patchpanel/dbus/client.h>

#include "shill/control_interface.h"
#include "shill/manager.h"
#include "shill/network/network.h"
#include "shill/network/network_manager.h"
#include "shill/network/network_monitor.h"
#include "shill/supplicant/supplicant_group_proxy_interface.h"
#include "shill/supplicant/supplicant_interface_proxy_interface.h"
#include "shill/supplicant/supplicant_p2pdevice_proxy_interface.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/technology.h"
#include "shill/wifi/wifi_provider.h"

namespace shill {

namespace {
const char* GroupInfoState(P2PDevice::P2PDeviceState state) {
  switch (state) {
    case P2PDevice::P2PDeviceState::kGOStarting:
      return kP2PGroupInfoStateStarting;
    case P2PDevice::P2PDeviceState::kGOConfiguring:
      return kP2PGroupInfoStateConfiguring;
    case P2PDevice::P2PDeviceState::kGOActive:
      return kP2PGroupInfoStateActive;
    case P2PDevice::P2PDeviceState::kGOStopping:
      return kP2PGroupInfoStateStopping;
    case P2PDevice::P2PDeviceState::kUninitialized:
    case P2PDevice::P2PDeviceState::kReady:
    case P2PDevice::P2PDeviceState::kClientAssociating:
    case P2PDevice::P2PDeviceState::kClientConfiguring:
    case P2PDevice::P2PDeviceState::kClientConnected:
    case P2PDevice::P2PDeviceState::kClientDisconnecting:
      return kP2PGroupInfoStateIdle;
  }
  NOTREACHED() << "Unhandled P2P state " << static_cast<int>(state);
  return kP2PGroupInfoStateIdle;
}

const char* ClientInfoState(P2PDevice::P2PDeviceState state) {
  switch (state) {
    case P2PDevice::P2PDeviceState::kClientAssociating:
      return kP2PClientInfoStateAssociating;
    case P2PDevice::P2PDeviceState::kClientConfiguring:
      return kP2PClientInfoStateConfiguring;
    case P2PDevice::P2PDeviceState::kClientConnected:
      return kP2PClientInfoStateConnected;
    case P2PDevice::P2PDeviceState::kClientDisconnecting:
      return kP2PClientInfoStateDisconnecting;
    case P2PDevice::P2PDeviceState::kUninitialized:
    case P2PDevice::P2PDeviceState::kReady:
    case P2PDevice::P2PDeviceState::kGOStarting:
    case P2PDevice::P2PDeviceState::kGOConfiguring:
    case P2PDevice::P2PDeviceState::kGOActive:
    case P2PDevice::P2PDeviceState::kGOStopping:
      return kP2PClientInfoStateIdle;
  }
  NOTREACHED() << "Unhandled P2P state " << static_cast<int>(state);
  return kP2PClientInfoStateIdle;
}
}  // namespace

// Constructor function
P2PDevice::P2PDevice(Manager* manager,
                     LocalDevice::IfaceType iface_type,
                     const std::string& primary_link_name,
                     uint32_t phy_index,
                     int32_t shill_id,
                     WiFiPhy::Priority priority,
                     LocalDevice::EventCallback callback)
    : LocalDevice(
          manager, iface_type, std::nullopt, phy_index, priority, callback),
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
  supplicant_interface_path_ = RpcIdentifier("");
  supplicant_p2pdevice_proxy_.reset();
  supplicant_group_proxy_.reset();
  supplicant_group_path_ = RpcIdentifier("");
  supplicant_persistent_group_path_ = RpcIdentifier("");
  group_ssid_ = "";
  group_bssid_ = std::nullopt;
  group_frequency_ = 0;
  group_passphrase_ = "";
  interface_address_ = std::nullopt;
  go_ipv4_address_ = std::nullopt;
  go_network_id_ = std::nullopt;
  LOG(INFO) << log_name() << ": P2PDevice created";
}

P2PDevice::~P2PDevice() {
  LOG(INFO) << log_name() << ": P2PDevice destroyed";
  if (client_network_) {
    client_network_->Stop();
    client_network_->UnregisterEventHandler(this);
  }
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

void P2PDevice::UpdateGroupNetworkInfo(
    const patchpanel::Client::DownstreamNetwork& downstream_network) {
  go_ipv4_address_ = downstream_network.ipv4_gateway_addr;
  go_network_id_ = downstream_network.network_id;
  // TODO(b/331859957): cache and expose IPv6 address.
}

Stringmaps P2PDevice::GroupInfoClients() const {
  Stringmaps clients;
  for (auto const& peer : group_peers_) {
    clients.push_back(peer.second.get()->GetPeerProperties());
  }
  return clients;
}

KeyValueStore P2PDevice::GetGroupInfo() const {
  KeyValueStore group_info;
  if (iface_type() != LocalDevice::IfaceType::kP2PGO) {
    LOG(WARNING) << log_name() << ": Tried to get group info for iface_type "
                 << iface_type();
    return group_info;
  }
  group_info.Set<int32_t>(kP2PGroupInfoShillIDProperty, shill_id());
  group_info.Set<String>(kP2PGroupInfoStateProperty, GroupInfoState(state_));

  if (IsLinkLayerConnected()) {
    group_info.Set<String>(kP2PGroupInfoSSIDProperty, group_ssid_);
    group_info.Set<String>(
        kP2PGroupInfoBSSIDProperty,
        group_bssid_.has_value() ? group_bssid_->ToString() : "");
    group_info.Set<Integer>(kP2PGroupInfoFrequencyProperty, group_frequency_);
    group_info.Set<String>(kP2PGroupInfoPassphraseProperty, group_passphrase_);
    group_info.Set<String>(kP2PGroupInfoInterfaceProperty, *link_name());
    group_info.Set<String>(kP2PClientInfoMACAddressProperty,
                           interface_address_.value().ToString());
    group_info.Set<Stringmaps>(kP2PGroupInfoClientsProperty,
                               GroupInfoClients());
    if (IsNetworkLayerConnected()) {
      if (go_network_id_ != std::nullopt) {
        group_info.Set<int32_t>(kP2PGroupInfoNetworkIDProperty,
                                go_network_id_.value());
      }
      if (go_ipv4_address_ != std::nullopt) {
        group_info.Set<String>(kP2PGroupInfoIPv4AddressProperty,
                               go_ipv4_address_.value().ToString());
      }
    }
  }

  return group_info;
}

KeyValueStore P2PDevice::GetClientInfo() const {
  KeyValueStore client_info;
  if (iface_type() != LocalDevice::IfaceType::kP2PClient) {
    LOG(WARNING) << log_name() << ": Tried to get client info for iface_type "
                 << iface_type();
    return client_info;
  }
  client_info.Set<int32_t>(kP2PClientInfoShillIDProperty, shill_id());
  client_info.Set<String>(kP2PClientInfoStateProperty, ClientInfoState(state_));

  if (IsLinkLayerConnected()) {
    Stringmap go_info;
    const std::string group_bssid_str =
        group_bssid_.has_value() ? group_bssid_->ToString() : "";
    client_info.Set<String>(kP2PClientInfoSSIDProperty, group_ssid_);
    client_info.Set<String>(kP2PClientInfoGroupBSSIDProperty, group_bssid_str);
    client_info.Set<Integer>(kP2PClientInfoFrequencyProperty, group_frequency_);
    client_info.Set<String>(kP2PClientInfoPassphraseProperty,
                            group_passphrase_);
    client_info.Set<String>(kP2PClientInfoInterfaceProperty, *link_name());
    client_info.Set<String>(kP2PClientInfoMACAddressProperty,
                            interface_address_.value().ToString());
    go_info.insert(
        {kP2PClientInfoGroupOwnerMACAddressProperty, group_bssid_str});
    if (IsNetworkLayerConnected()) {
      client_info.Set<int32_t>(kP2PClientInfoNetworkIDProperty,
                               client_network_->network_id());
      const auto network_config = client_network_->GetNetworkConfig();
      if (network_config.ipv4_address != std::nullopt) {
        client_info.Set<String>(
            kP2PClientInfoIPv4AddressProperty,
            network_config.ipv4_address->address().ToString());
      }
      if (!network_config.ipv6_addresses.empty()) {
        Strings ipv6_addresses;
        for (auto ipv6_address : network_config.ipv6_addresses) {
          ipv6_addresses.push_back(ipv6_address.address().ToString());
        }
        client_info.Set<Strings>(kP2PClientInfoIPv6AddressProperty,
                                 ipv6_addresses);
      }
      if (network_config.ipv4_gateway != std::nullopt) {
        go_info.insert({kP2PClientInfoGroupOwnerIPv4AddressProperty,
                        network_config.ipv4_gateway->ToString()});
      }
      if (network_config.ipv6_gateway != std::nullopt) {
        go_info.insert({kP2PClientInfoGroupOwnerIPv6AddressProperty,
                        network_config.ipv6_gateway->ToString()});
      }
    }
    client_info.Set<Stringmap>(kP2PClientInfoGroupOwnerProperty, go_info);
  }

  return client_info;
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
  if (!service) {
    LOG(ERROR) << log_name()
               << ": Tried to create a group with an empty service.";
    return false;
  }
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
  return true;
}

bool P2PDevice::Connect(std::unique_ptr<P2PService> service) {
  if (state_ != P2PDeviceState::kReady) {
    LOG(ERROR) << log_name() << ": Tried to connect while in state "
               << P2PDeviceStateName(state_);
    return false;
  }
  if (!service) {
    LOG(ERROR) << log_name() << ": Tried to connect with an empty serveice.";
    return false;
  }
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
  return true;
}

bool P2PDevice::RemoveGroup() {
  if (!InGOState()) {
    LOG(WARNING) << log_name() << ": Tried to remove a group while in state "
                 << P2PDeviceStateName(state_);
    return false;
  }
  SetState(P2PDeviceState::kGOStopping);
  go_ipv4_address_ = std::nullopt;
  go_network_id_ = std::nullopt;
  group_network_fd_.reset();
  FinishSupplicantGroup();
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
  SetState(P2PDeviceState::kClientDisconnecting);
  if (client_network_) {
    client_network_->Stop();
    client_network_->UnregisterEventHandler(this);
    client_network_.reset();
  }
  FinishSupplicantGroup();
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

bool P2PDevice::IsLinkLayerConnected() const {
  if (iface_type() == LocalDevice::IfaceType::kP2PClient) {
    return (state_ == P2PDeviceState::kClientConfiguring ||
            state_ == P2PDeviceState::kClientConnected);
  } else if (iface_type() == LocalDevice::IfaceType::kP2PGO) {
    return (state_ == P2PDeviceState::kGOConfiguring ||
            state_ == P2PDeviceState::kGOActive);
  } else {
    return false;
  }
}

bool P2PDevice::IsNetworkLayerConnected() const {
  if (iface_type() == LocalDevice::IfaceType::kP2PClient) {
    return (state_ == P2PDeviceState::kClientConnected);
  } else if (iface_type() == LocalDevice::IfaceType::kP2PGO) {
    return (state_ == P2PDeviceState::kGOActive);
  } else {
    return false;
  }
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
  supplicant_interface_path_ = object_path;
  LOG(INFO) << log_name() << ": Interface proxy connected, path: "
            << supplicant_interface_path_.value();
  return true;
}

void P2PDevice::DisconnectFromSupplicantInterfaceProxy() {
  if (supplicant_interface_proxy_) {
    LOG(INFO) << log_name() << ": Interface proxy disconnected, path: "
              << supplicant_interface_path_.value();
  }
  supplicant_interface_path_ = RpcIdentifier("");
  supplicant_interface_proxy_.reset();
}

String P2PDevice::GetInterfaceName() const {
  String ifname;
  if (!supplicant_interface_proxy_->GetIfname(&ifname)) {
    LOG(ERROR) << log_name() << ": Failed to GetIfname via Interface proxy";
    return "";
  }
  return ifname;
}

std::optional<net_base::MacAddress> P2PDevice::GetInterfaceAddress() const {
  ByteArray mac_address;
  if (!supplicant_interface_proxy_->GetMACAddress(&mac_address)) {
    LOG(ERROR) << log_name()
               << ": Failed to Get MAC address via Interface proxy";
    return std::nullopt;
  }
  return net_base::MacAddress::CreateFromBytes(mac_address);
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
  supplicant_group_path_ = group;
  LOG(INFO) << log_name() << ": Group proxy connected, path: "
            << supplicant_group_path_.value();
  return true;
}

void P2PDevice::DisconnectFromSupplicantGroupProxy(void) {
  if (supplicant_group_proxy_) {
    LOG(INFO) << log_name() << ": Group proxy disconnected, path: "
              << supplicant_group_path_.value();
  }
  supplicant_group_path_ = RpcIdentifier("");
  supplicant_group_proxy_.reset();
}

String P2PDevice::GetGroupSSID() const {
  ByteArray ssid;
  if (!supplicant_group_proxy_->GetSSID(&ssid)) {
    LOG(ERROR) << log_name() << ": Failed to GetSSID via Group proxy";
    return "";
  }
  return net_base::byte_utils::ByteStringFromBytes(ssid);
}

std::optional<net_base::MacAddress> P2PDevice::GetGroupBSSID() const {
  ByteArray bssid;
  if (!supplicant_group_proxy_->GetBSSID(&bssid)) {
    LOG(ERROR) << log_name() << ": Failed to GetBSSID via Group proxy";
    return std::nullopt;
  }
  return net_base::MacAddress::CreateFromBytes(bssid);
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
  if (link_name_.value().empty()) {
    LOG(ERROR) << log_name() << ": Failed to get interface name";
    return false;
  } else {
    LOG(INFO) << log_name() << ": Link name configured: " << link_name_.value();
  }

  group_ssid_ = GetGroupSSID();
  if (group_ssid_.empty()) {
    LOG(ERROR) << log_name() << ": Failed to get group SSID";
    return false;
  } else {
    LOG(INFO) << log_name() << ": SSID configured: " << group_ssid_;
  }

  group_bssid_ = GetGroupBSSID();
  if (!group_bssid_.has_value()) {
    LOG(ERROR) << log_name() << ": Failed to get group BSSID";
    return false;
  } else {
    LOG(INFO) << log_name()
              << ": BSSID configured: " << group_bssid_->ToString();
  }

  group_frequency_ = GetGroupFrequency();
  if (group_frequency_) {
    LOG(INFO) << log_name() << ": Freqency configured: " << group_frequency_;
  } else {
    LOG(ERROR) << log_name() << ": Failed to get group frequency";
    return false;
  }

  group_passphrase_ = GetGroupPassphrase();
  if (group_passphrase_.empty()) {
    LOG(ERROR) << log_name() << ": Failed to get group passphrase";
    return false;
  } else {
    LOG(INFO) << log_name() << ": Passphrase configured: " << group_passphrase_;
  }

  interface_address_ = GetInterfaceAddress();
  if (interface_address_ == std::nullopt) {
    LOG(ERROR) << log_name() << ": Failed to get interface address";
    return false;
  } else {
    LOG(INFO) << log_name() << ": Interface address configured: "
              << interface_address_.value().ToString();
  }

  // TODO(b/308081318): This requires HotspotDevice to be fully responsible
  // for states and events handling. Currently DeviceEvent::kLinkUp/Down events
  // are partially handled by LocalService.
  // service_->SetState(LocalService::LocalServiceState::kStateUp);
  return true;
}

void P2PDevice::TeardownGroup(const KeyValueStore& properties) {
  RpcIdentifier interface_path = RpcIdentifier("");
  if (properties.Contains<RpcIdentifier>(
          WPASupplicant::kGroupFinishedPropertyInterfaceObject)) {
    interface_path = properties.Get<RpcIdentifier>(
        WPASupplicant::kGroupFinishedPropertyInterfaceObject);
  }
  CHECK(interface_path == supplicant_interface_path_);
  RpcIdentifier group_path = RpcIdentifier("");
  if (properties.Contains<RpcIdentifier>(
          WPASupplicant::kGroupFinishedPropertyGroupObject)) {
    group_path = properties.Get<RpcIdentifier>(
        WPASupplicant::kGroupFinishedPropertyGroupObject);
  }
  if (group_path != supplicant_group_path_) {
    LOG(WARNING) << log_name() << ": " << __func__
                 << " for unknown object, path: " << group_path.value();
  }
  TeardownGroup();
}

void P2PDevice::TeardownGroup() {
  // TODO(b/322557062): Ensure that the underlying kernel interface is properly
  // torn down.
  group_ssid_ = "";
  group_bssid_ = std::nullopt;
  group_frequency_ = 0;
  group_passphrase_ = "";
  group_peers_.clear();
  link_name_ = std::nullopt;

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
  switch (state_) {
    // Expected P2P client state for GroupStarted event
    case P2PDeviceState::kClientAssociating:
      SetupGroup(properties);
      SetState(P2PDeviceState::kClientConfiguring);
      PostDeviceEvent(DeviceEvent::kLinkUp);
      AcquireClientIP();
      break;
    // Expected P2P GO state for GroupStarted event
    case P2PDeviceState::kGOStarting:
      SetupGroup(properties);
      SetState(P2PDeviceState::kGOConfiguring);
      PostDeviceEvent(DeviceEvent::kLinkUp);
      StartGroupNetwork();
      break;
    // Common states for all roles.
    case P2PDeviceState::kUninitialized:
    case P2PDeviceState::kReady:
    // P2P client states.
    case P2PDeviceState::kClientConfiguring:
    case P2PDeviceState::kClientConnected:
    case P2PDeviceState::kClientDisconnecting:
    // P2P GO states.
    case P2PDeviceState::kGOConfiguring:
    case P2PDeviceState::kGOActive:
    case P2PDeviceState::kGOStopping:
      LOG(WARNING) << log_name() << ": Ignored " << __func__
                   << " while in state " << P2PDeviceStateName(state_);
      break;
  }
}

void P2PDevice::GroupFinished(const KeyValueStore& properties) {
  LOG(INFO) << log_name() << ": Got " << __func__ << " while in state "
            << P2PDeviceStateName(state_);
  switch (state_) {
    // Expected P2P client/GO state for GroupFinished event
    case P2PDeviceState::kClientDisconnecting:
    case P2PDeviceState::kGOStopping:
      TeardownGroup(properties);
      SetState(P2PDeviceState::kReady);
      PostDeviceEvent(DeviceEvent::kLinkDown);
      break;
    // P2P client link failure states for GroupFinished event
    case P2PDeviceState::kClientConfiguring:
    case P2PDeviceState::kClientConnected:
      LOG(WARNING) << log_name()
                   << ": Client link failure, group finished while in state "
                   << P2PDeviceStateName(state_);
      TeardownGroup(properties);
      SetState(P2PDeviceState::kClientDisconnecting);
      PostDeviceEvent(DeviceEvent::kLinkFailure);
      break;
    // P2P GO link failure states for GroupFinished event
    case P2PDeviceState::kGOConfiguring:
    case P2PDeviceState::kGOActive:
      LOG(WARNING) << log_name()
                   << ": GO link failure, group finished while in state "
                   << P2PDeviceStateName(state_);
      TeardownGroup(properties);
      SetState(P2PDeviceState::kGOStopping);
      PostDeviceEvent(DeviceEvent::kLinkFailure);
      break;
    // P2P client/GO unknown error states for GroupFinished event
    case P2PDeviceState::kClientAssociating:
    case P2PDeviceState::kGOStarting:
      LOG(ERROR) << log_name() << ": Ignored " << __func__ << " while in state "
                 << P2PDeviceStateName(state_);
      break;
    // Common states for all roles.
    case P2PDeviceState::kUninitialized:
    case P2PDeviceState::kReady:
      LOG(WARNING) << log_name() << ": Ignored " << __func__
                   << " while in state " << P2PDeviceStateName(state_);
      break;
  }
}

void P2PDevice::GroupFormationFailure(const std::string& reason) {
  LOG(WARNING) << log_name() << ": Got " << __func__ << " while in state "
               << P2PDeviceStateName(state_);
  switch (state_) {
    // Expected P2P client state for GroupFormationFailure signal
    case P2PDeviceState::kClientAssociating:
      LOG(ERROR) << log_name()
                 << ": Failed to connect Client, group formation failure";
      SetState(P2PDeviceState::kClientDisconnecting);
      PostDeviceEvent(DeviceEvent::kLinkFailure);
      break;
    // Expected P2P GO state for GroupFormationFailure signal
    case P2PDeviceState::kGOStarting:
      LOG(ERROR) << log_name()
                 << ": Failed to start GO, group formation failure";
      SetState(P2PDeviceState::kGOStopping);
      PostDeviceEvent(DeviceEvent::kLinkFailure);
      break;
    // Common states for all roles.
    case P2PDeviceState::kUninitialized:
    case P2PDeviceState::kReady:
    // P2P client states.
    case P2PDeviceState::kClientConfiguring:
    case P2PDeviceState::kClientConnected:
    case P2PDeviceState::kClientDisconnecting:
    // P2P GO states.
    case P2PDeviceState::kGOConfiguring:
    case P2PDeviceState::kGOActive:
    case P2PDeviceState::kGOStopping:
      LOG(WARNING) << log_name() << ": Ignored " << __func__
                   << " while in state " << P2PDeviceStateName(state_);
      break;
  }
}

void P2PDevice::AcquireClientIP() {
  if (client_network_for_test_) {
    client_network_ = std::move(client_network_for_test_);
    client_network_->RegisterEventHandler(this);
  } else {
    // TODO(b/310584119): use new enum instead of Technology inside
    // shill::Network
    client_network_ = manager()->network_manager()->CreateNetwork(
        net_base::RTNLHandler::GetInstance()->GetInterfaceIndex(
            link_name().value()),
        link_name().value(), Technology::kWiFi, false,
        manager()->patchpanel_client());
    client_network_->RegisterEventHandler(this);
  }

  const Network::StartOptions opts = {
      .dhcp = manager()->CreateDefaultDHCPOption(),
      .accept_ra = true,
      .ignore_link_monitoring = true,
      // TODO(b/314693271) omit probing_configuration when validation mode is
      // kDisabled.
      .probing_configuration =
          manager()->GetPortalDetectorProbingConfiguration(),
      .validation_mode = NetworkMonitor::ValidationMode::kDisabled,
  };
  client_network_->Start(opts);
}

bool P2PDevice::StartGroupNetwork() {
  if (!manager()->patchpanel_client() ||
      !manager()->patchpanel_client()->CreateLocalOnlyNetwork(
          link_name().value(), base::BindOnce(&P2PDevice::OnGroupNetworkStarted,
                                              base::Unretained(this)))) {
    LOG(ERROR) << log_name() << ": Failed to create local only network";
    PostDeviceEvent(DeviceEvent::kNetworkFailure);
    return false;
  }

  return true;
}

void P2PDevice::OnConnectionUpdated(int net_interface_index) {
  if (state_ != P2PDeviceState::kClientConfiguring) {
    LOG(WARNING) << log_name() << ": Ignored " << __func__ << " while in state "
                 << P2PDeviceStateName(state_);
    return;
  }

  LOG(INFO) << log_name() << ": Successfully get IP address on "
            << link_name().value();
  SetState(P2PDeviceState::kClientConnected);
  PostDeviceEvent(DeviceEvent::kNetworkUp);
}

void P2PDevice::OnNetworkStopped(int interface_index, bool is_failure) {
  if (state_ == P2PDeviceState::kClientConfiguring) {
    // Failed to fetch an IP address for client mode.
    PostDeviceEvent(DeviceEvent::kNetworkFailure);
    TeardownGroup();
  } else if (state_ == P2PDeviceState::kClientConnected) {
    PostDeviceEvent(DeviceEvent::kNetworkDown);
    TeardownGroup();
  } else {
    LOG(WARNING) << log_name() << ": Ignored " << __func__
                 << " (failure = " << is_failure << ") while in state "
                 << P2PDeviceStateName(state_);
  }
}

void P2PDevice::OnGroupNetworkStarted(
    base::ScopedFD network_fd,
    const patchpanel::Client::DownstreamNetwork& network) {
  if (state_ != P2PDeviceState::kGOConfiguring) {
    LOG(WARNING) << log_name() << ": Ignored " << __func__ << " while in state "
                 << P2PDeviceStateName(state_);
    return;
  }

  if (!network_fd.is_valid()) {
    LOG(ERROR) << log_name() << ": Failed to create group network on "
               << link_name().value();
    PostDeviceEvent(DeviceEvent::kNetworkFailure);
    return;
  }

  LOG(INFO) << log_name() << ": Established downstream network network_id="
            << network.network_id << " on " << link_name().value();
  group_network_fd_ = std::move(network_fd);
  UpdateGroupNetworkInfo(network);
  SetState(P2PDeviceState::kGOActive);
  PostDeviceEvent(DeviceEvent::kNetworkUp);
}

void P2PDevice::NetworkFinished() {
  LOG(INFO) << log_name() << ": Got " << __func__ << " while in state "
            << P2PDeviceStateName(state_);
  // TODO(b/308081318): teardown group/connection or ignore unexpected state
  PostDeviceEvent(DeviceEvent::kNetworkDown);
}

void P2PDevice::NetworkFailure(const std::string& reason) {
  LOG(WARNING) << log_name() << ": Got " << __func__ << " while in state "
               << P2PDeviceStateName(state_) << ", reason: " << reason;
  // TODO(b/308081318): teardown group/connection or ignore unexpected state
  PostDeviceEvent(DeviceEvent::kNetworkFailure);
}

void P2PDevice::PeerJoined(const dbus::ObjectPath& peer) {
  LOG(INFO) << log_name() << ": Got " << __func__ << " while in state "
            << P2PDeviceStateName(state_);
  if (state_ != P2PDeviceState::kGOConfiguring &&
      state_ != P2PDeviceState::kGOActive) {
    return;
  }

  if (base::Contains(group_peers_, peer)) {
    LOG(WARNING) << "Ignored " << __func__
                 << " while already connected, path: " << peer.value();
    return;
  }
  group_peers_[peer] =
      std::make_unique<P2PPeer>(this, peer, ControlInterface());
  LOG(INFO) << log_name() << ": Peer connected, path: " << peer.value();
  PostDeviceEvent(DeviceEvent::kPeerConnected);
}

void P2PDevice::PeerDisconnected(const dbus::ObjectPath& peer) {
  LOG(INFO) << log_name() << ": Got " << __func__ << " while in state "
            << P2PDeviceStateName(state_);

  if (state_ != P2PDeviceState::kGOConfiguring &&
      state_ != P2PDeviceState::kGOActive) {
    return;
  }

  if (!base::Contains(group_peers_, peer)) {
    LOG(WARNING) << "Ignored " << __func__
                 << " while not connected, path: " << peer.value();
    return;
  }
  LOG(INFO) << log_name() << ": Peer disconnected, path: " << peer.value();
  group_peers_.erase(peer);
  PostDeviceEvent(DeviceEvent::kPeerDisconnected);
}

}  // namespace shill
