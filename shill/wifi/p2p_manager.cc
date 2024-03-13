// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_manager.h"

#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/logging.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/manager.h"
#include "shill/store/property_accessor.h"
#include "shill/supplicant/supplicant_p2pdevice_proxy_interface.h"
#include "shill/supplicant/supplicant_process_proxy_interface.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/local_device.h"

namespace shill {

P2PManager::P2PManager(Manager* manager)
    : manager_(manager),
      allowed_(false),
      next_unique_id_(0),
      supplicant_primary_p2pdevice_pending_event_delegate_(nullptr) {
  supplicant_primary_p2pdevice_proxy_.reset();
}

P2PManager::~P2PManager() = default;

void P2PManager::InitPropertyStore(PropertyStore* store) {
  HelpRegisterDerivedBool(store, kP2PAllowedProperty, &P2PManager::GetAllowed,
                          &P2PManager::SetAllowed);
  HelpRegisterDerivedKeyValueStore(store, kP2PCapabilitiesProperty,
                                   &P2PManager::GetCapabilities, nullptr);
  HelpRegisterDerivedKeyValueStores(store, kP2PGroupInfosProperty,
                                    &P2PManager::GetGroupInfos, nullptr);
  HelpRegisterDerivedKeyValueStores(store, kP2PClientInfosProperty,
                                    &P2PManager::GetClientInfos, nullptr);
}

bool P2PManager::IsP2PSupported() {
  auto wifi_phys = manager_->wifi_provider()->GetPhys();
  if (!wifi_phys.empty()) {
    return wifi_phys.front()->SupportP2PMode();
  }
  LOG(ERROR) << "No WiFiPhy available";
  return false;
}

String P2PManager::GroupReadiness() {
  // TODO(b/295050788, b/299295629): it requires P2P/STA concurrency level
  // and interface combination checking to be supported by wifi phy.
  return kP2PCapabilitiesGroupReadinessNotReady;
}

String P2PManager::ClientReadiness() {
  // TODO(b/295050788, b/299295629): it requires P2P/STA concurrency level
  // and interface combination checking to be supported by wifi phy.
  return kP2PCapabilitiesClientReadinessNotReady;
}

Integers P2PManager::SupportedChannels() {
  // TODO(b/295050788, b/299295629): it requires P2P/STA concurrency level
  // and interface combination checking to be supported by wifi phy.
  Integers channels;
  return channels;
}

Integers P2PManager::PreferredChannels() {
  // TODO(b/295050788, b/299295629): it requires P2P/STA concurrency level
  // and interface combination checking to be supported by wifi phy.
  Integers channels;
  return channels;
}

KeyValueStore P2PManager::GetCapabilities(Error* /* error */) {
  KeyValueStore caps;
  if (IsP2PSupported()) {
    caps.Set<Boolean>(kP2PCapabilitiesP2PSupportedProperty, true);
    caps.Set<String>(kP2PCapabilitiesGroupReadinessProperty, GroupReadiness());
    caps.Set<String>(kP2PCapabilitiesClientReadinessProperty,
                     ClientReadiness());
    caps.Set<Integers>(kP2PCapabilitiesSupportedChannelsProperty,
                       SupportedChannels());
    caps.Set<Integers>(kP2PCapabilitiesPreferredChannelsProperty,
                       PreferredChannels());
  } else {
    caps.Set<Boolean>(kP2PCapabilitiesP2PSupportedProperty, false);
  }
  return caps;
}

KeyValueStores P2PManager::GetGroupInfos(Error* /* error */) {
  KeyValueStores groupInfos;
  for (const auto& it : p2p_group_owners_) {
    groupInfos.push_back(it.second->GetGroupInfo());
  }
  return groupInfos;
}

KeyValueStores P2PManager::GetClientInfos(Error* /* error */) {
  KeyValueStores clientInfos;
  for (const auto& it : p2p_clients_) {
    clientInfos.push_back(it.second->GetClientInfo());
  }
  return clientInfos;
}

void P2PManager::Start() {}

void P2PManager::Stop() {
  // TODO(b/308081318) Cleanup active sessions.
  if (!p2p_group_owners_.empty() || !p2p_clients_.empty()) {
    LOG(WARNING) << "P2PManager has been stopped while some of P2P devices "
                    "are still active";
  }
}

void P2PManager::CreateP2PGroup(P2PResultCallback callback,
                                const KeyValueStore& args) {
  LOG(INFO) << __func__;
  CHECK(callback) << "Callback is empty";

  if (supplicant_primary_p2pdevice_pending_event_delegate_ ||
      result_callback_) {
    LOG(WARNING) << "Failed to create P2P group, operation is already "
                    "in progress";
    PostResult(kCreateP2PGroupResultOperationInProgress, std::nullopt,
               std::move(callback));
    return;
  }

  result_callback_ = std::move(callback);
  std::optional<std::string> ssid;
  if (args.Contains<std::string>(kP2PDeviceSSID)) {
    ssid = args.Get<std::string>(kP2PDeviceSSID);
  }

  std::optional<std::string> passphrase;
  if (args.Contains<std::string>(kP2PDevicePassphrase)) {
    passphrase = args.Get<std::string>(kP2PDevicePassphrase);
  }

  std::optional<uint32_t> freq;
  if (args.Contains<uint32_t>(kP2PDeviceFrequency)) {
    freq = args.Get<uint32_t>(kP2PDeviceFrequency);
  }

  std::optional<uint32_t> priority;
  if (args.Contains<uint32_t>(kP2PDevicePriority)) {
    priority = args.Get<uint32_t>(kP2PDevicePriority);
    LOG(INFO) << "Priority argument value: " << *priority;
  }

  if (!ConnectToSupplicantPrimaryP2PDeviceProxy()) {
    LOG(ERROR) << "Failed to create P2P group, primary P2PDevice proxy "
                  "is not connected";
    PostResult(kCreateP2PGroupResultOperationFailed, std::nullopt,
               std::move(callback));
    return;
  }

  P2PDeviceRefPtr p2p_dev = manager_->wifi_provider()->CreateP2PDevice(
      LocalDevice::IfaceType::kP2PGO,
      base::BindRepeating(&P2PManager::OnP2PDeviceEvent,
                          base::Unretained(this)),
      next_unique_id_);
  next_unique_id_++;

  if (!p2p_dev) {
    LOG(ERROR) << "Failed to create a WiFi P2P interface.";
    PostResult(kCreateP2PGroupResultOperationFailed, std::nullopt,
               std::move(result_callback_));
    DisconnectFromSupplicantPrimaryP2PDeviceProxy();
    return;
  }
  if (!p2p_dev->SetEnabled(true)) {
    LOG(ERROR) << "Failed to enable a WiFi P2P interface.";
    PostResult(kCreateP2PGroupResultOperationFailed, std::nullopt,
               std::move(result_callback_));
    DisconnectFromSupplicantPrimaryP2PDeviceProxy();
    return;
  }
  p2p_group_owners_[p2p_dev->shill_id()] = p2p_dev;
  std::unique_ptr<P2PService> service =
      std::make_unique<P2PService>(p2p_dev, ssid, passphrase, freq);
  if (!p2p_dev->CreateGroup(std::move(service))) {
    LOG(ERROR) << "Failed to initiate group creation";
    PostResult(kCreateP2PGroupResultOperationFailed, std::nullopt,
               std::move(result_callback_));
    DeleteP2PDevice(p2p_dev);
    return;
  }
  supplicant_primary_p2pdevice_pending_event_delegate_ = p2p_dev.get();
}

void P2PManager::ConnectToP2PGroup(P2PResultCallback callback,
                                   const KeyValueStore& args) {
  LOG(INFO) << __func__;
  CHECK(callback) << "Callback is empty";

  if (supplicant_primary_p2pdevice_pending_event_delegate_ ||
      result_callback_) {
    LOG(WARNING) << "Failed to connect to P2P group, operation is already "
                    "in progress";
    PostResult(kConnectToP2PGroupResultOperationInProgress, std::nullopt,
               std::move(callback));
    return;
  }

  result_callback_ = std::move(callback);
  if (!args.Contains<std::string>(kP2PDeviceSSID)) {
    LOG(ERROR) << std::string(kP2PDeviceSSID) + " argument is mandatory";
    PostResult(kConnectToP2PGroupResultInvalidArguments, std::nullopt,
               std::move(result_callback_));
    return;
  }
  std::string ssid = args.Get<std::string>(kP2PDeviceSSID);

  if (!args.Contains<std::string>(kP2PDevicePassphrase)) {
    LOG(ERROR) << std::string(kP2PDevicePassphrase) + " argument is mandatory";
    PostResult(kConnectToP2PGroupResultInvalidArguments, std::nullopt,
               std::move(result_callback_));
    return;
  }
  std::string passphrase = args.Get<std::string>(kP2PDevicePassphrase);

  std::optional<uint32_t> freq;
  if (args.Contains<uint32_t>(kP2PDeviceFrequency)) {
    freq = args.Get<uint32_t>(kP2PDeviceFrequency);
  }

  std::optional<uint32_t> priority;
  if (args.Contains<uint32_t>(kP2PDevicePriority)) {
    priority = args.Get<uint32_t>(kP2PDevicePriority);
    LOG(INFO) << "Prioirty argument value: " << *priority;
  }

  if (!ConnectToSupplicantPrimaryP2PDeviceProxy()) {
    LOG(ERROR) << "Failed to connect to P2P group, primary P2PDevice proxy "
                  "is not connected";
    PostResult(kConnectToP2PGroupResultOperationFailed, std::nullopt,
               std::move(callback));
    return;
  }

  P2PDeviceRefPtr p2p_dev = manager_->wifi_provider()->CreateP2PDevice(
      LocalDevice::IfaceType::kP2PClient,
      base::BindRepeating(&P2PManager::OnP2PDeviceEvent,
                          base::Unretained(this)),
      next_unique_id_);
  next_unique_id_++;
  if (!p2p_dev) {
    LOG(ERROR) << "Failed to create a WiFi P2P interface.";
    PostResult(kConnectToP2PGroupResultOperationFailed, std::nullopt,
               std::move(result_callback_));
    DisconnectFromSupplicantPrimaryP2PDeviceProxy();
    return;
  }
  if (!p2p_dev->SetEnabled(true)) {
    LOG(ERROR) << "Failed to enable a WiFi P2P interface.";
    PostResult(kConnectToP2PGroupResultOperationFailed, std::nullopt,
               std::move(result_callback_));
    DisconnectFromSupplicantPrimaryP2PDeviceProxy();
    return;
  }
  p2p_clients_[p2p_dev->shill_id()] = p2p_dev;
  std::unique_ptr<P2PService> service =
      std::make_unique<P2PService>(p2p_dev, ssid, passphrase, freq);
  if (!p2p_dev->Connect(std::move(service))) {
    LOG(ERROR) << "Failed to initiate connection";
    PostResult(kConnectToP2PGroupResultOperationFailed, std::nullopt,
               std::move(result_callback_));
    DeleteP2PDevice(p2p_dev);
    return;
  }
  supplicant_primary_p2pdevice_pending_event_delegate_ = p2p_dev.get();
}

void P2PManager::DestroyP2PGroup(P2PResultCallback callback,
                                 uint32_t shill_id) {
  LOG(INFO) << __func__;
  CHECK(callback) << "Callback is empty";

  if (result_callback_) {
    PostResult(kDestroyP2PGroupResultOperationInProgress, std::nullopt,
               std::move(callback));
    return;
  }
  result_callback_ = std::move(callback);
  if (!base::Contains(p2p_group_owners_, shill_id)) {
    LOG(ERROR) << "There is no P2P group at the requested shill_id: "
               << shill_id;
    PostResult(kDestroyP2PGroupResultNoGroup, std::nullopt,
               std::move(result_callback_));
    return;
  }
  p2p_group_owners_[shill_id]->RemoveGroup();
}

void P2PManager::DisconnectFromP2PGroup(P2PResultCallback callback,
                                        uint32_t shill_id) {
  LOG(INFO) << __func__;
  CHECK(callback) << "Callback is empty";

  if (result_callback_) {
    PostResult(kDisconnectFromP2PGroupResultOperationInProgress, std::nullopt,
               std::move(callback));
    return;
  }
  result_callback_ = std::move(callback);
  if (p2p_clients_.find(shill_id) == p2p_clients_.end()) {
    LOG(ERROR) << "There is no P2P client at the requested shill_id: "
               << shill_id;
    PostResult(kDisconnectFromP2PGroupResultNotConnected, std::nullopt,
               std::move(result_callback_));
    return;
  }
  p2p_clients_[shill_id]->Disconnect();
}

void P2PManager::HelpRegisterDerivedBool(PropertyStore* store,
                                         std::string_view name,
                                         bool (P2PManager::*get)(Error* error),
                                         bool (P2PManager::*set)(const bool&,
                                                                 Error*)) {
  store->RegisterDerivedBool(
      name, BoolAccessor(new CustomAccessor<P2PManager, bool>(this, get, set)));
}

void P2PManager::HelpRegisterDerivedKeyValueStore(
    PropertyStore* store,
    std::string_view name,
    KeyValueStore (P2PManager::*get)(Error* error),
    bool (P2PManager::*set)(const KeyValueStore&, Error*)) {
  store->RegisterDerivedKeyValueStore(
      name, KeyValueStoreAccessor(
                new CustomAccessor<P2PManager, KeyValueStore>(this, get, set)));
}

void P2PManager::HelpRegisterDerivedKeyValueStores(
    PropertyStore* store,
    std::string_view name,
    KeyValueStores (P2PManager::*get)(Error* error),
    bool (P2PManager::*set)(const KeyValueStores&, Error*)) {
  store->RegisterDerivedKeyValueStores(
      name,
      KeyValueStoresAccessor(
          new CustomAccessor<P2PManager, KeyValueStores>(this, get, set)));
}

bool P2PManager::SetAllowed(const bool& value, Error* error) {
  if (allowed_ == value)
    return false;

  LOG(INFO) << __func__ << " Allowed set to " << std::boolalpha << value;
  allowed_ = value;
  Stop();
  return true;
}

void P2PManager::PostResult(std::string result_code,
                            std::optional<uint32_t> shill_id,
                            P2PResultCallback callback) {
  if (!callback) {
    LOG(ERROR) << "Callback is not set";
    return;
  }
  KeyValueStore response_dict;
  response_dict.Set<std::string>(kP2PResultCode, result_code);
  if (shill_id) {
    response_dict.Set<uint32_t>(kP2PDeviceShillID, *shill_id);
  }
  manager_->dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), response_dict));
}

void P2PManager::DeleteP2PDevice(P2PDeviceRefPtr p2p_dev) {
  if (!p2p_dev)
    return;

  manager_->wifi_provider()->DeleteLocalDevice(p2p_dev);
  if (p2p_dev->iface_type() == LocalDevice::IfaceType::kP2PGO) {
    p2p_group_owners_.erase(p2p_dev->shill_id());
  } else {
    p2p_clients_.erase(p2p_dev->shill_id());
  }

  DisconnectFromSupplicantPrimaryP2PDeviceProxy();
}

std::string P2PManager::PrimaryLinkName() const {
  return manager_->wifi_provider()->GetPrimaryLinkName();
}

SupplicantProcessProxyInterface* P2PManager::SupplicantProcessProxy() const {
  return manager_->supplicant_manager()->proxy();
}

ControlInterface* P2PManager::ControlInterface() const {
  return manager_->control_interface();
}

bool P2PManager::ConnectToSupplicantPrimaryP2PDeviceProxy() {
  if (supplicant_primary_p2pdevice_proxy_) {
    LOG(INFO) << "Primary P2PDevice proxy is already connected";
    return true;
  }
  std::string link_name = PrimaryLinkName();
  if (link_name.empty()) {
    LOG(ERROR) << "Failed to get the primary link name for WiFi technology";
    return false;
  }
  // TODO(b/311161440) Centralize the primary interface proxy ownership
  // in WiFiProvider so that all interfaces can access it without having to
  // create their own connection.
  RpcIdentifier interface_path;
  if (!SupplicantProcessProxy()->GetInterface(link_name, &interface_path)) {
    // Connect wpa_supplicant to the primary interface.
    KeyValueStore create_interface_args;
    create_interface_args.Set<std::string>(
        WPASupplicant::kInterfacePropertyName, link_name);
    create_interface_args.Set<std::string>(
        WPASupplicant::kInterfacePropertyDriver, WPASupplicant::kDriverNL80211);
    create_interface_args.Set<std::string>(
        WPASupplicant::kInterfacePropertyConfigFile,
        WPASupplicant::kSupplicantConfPath);
    if (!SupplicantProcessProxy()->CreateInterface(create_interface_args,
                                                   &interface_path)) {
      LOG(ERROR) << "Cannot connect to the primary interface " << link_name;
      return false;
    }
  }
  supplicant_primary_p2pdevice_proxy_ =
      ControlInterface()->CreateSupplicantP2PDeviceProxy(this, interface_path);
  if (!supplicant_primary_p2pdevice_proxy_) {
    LOG(ERROR) << "Failed to connect to the primary P2PDevice proxy: "
               << interface_path.value();
    return false;
  }
  LOG(INFO) << "Primary P2PDevice proxy connected: " << interface_path.value();
  return true;
}

void P2PManager::DisconnectFromSupplicantPrimaryP2PDeviceProxy() {
  if (supplicant_primary_p2pdevice_proxy_ && p2p_group_owners_.empty() &&
      p2p_clients_.empty()) {
    supplicant_primary_p2pdevice_proxy_.reset();
    LOG(INFO) << "Primary P2PDevice proxy disconnected";
  }
}

void P2PManager::GroupStarted(const KeyValueStore& properties) {
  RpcIdentifier interface_path = RpcIdentifier("");
  if (properties.Contains<RpcIdentifier>(
          WPASupplicant::kGroupStartedPropertyInterfaceObject)) {
    interface_path = properties.Get<RpcIdentifier>(
        WPASupplicant::kGroupStartedPropertyInterfaceObject);
  }
  if (interface_path.value().empty()) {
    LOG(WARNING) << "Ignored " << __func__ << " without interface";
    return;
  }
  if (base::Contains(supplicant_primary_p2pdevice_event_delegates_,
                     interface_path)) {
    LOG(WARNING) << "Ignored " << __func__
                 << " with assigned interface: " << interface_path.value();
    return;
  }
  auto delegate = supplicant_primary_p2pdevice_pending_event_delegate_;
  if (!delegate) {
    LOG(WARNING) << "Ignored " << __func__ << " while not expected, interface: "
                 << interface_path.value();
    return;
  }
  supplicant_primary_p2pdevice_pending_event_delegate_ = nullptr;
  supplicant_primary_p2pdevice_event_delegates_[interface_path] = delegate;

  LOG(INFO) << "Got " << __func__ << ", interface: " << interface_path.value();
  delegate->GroupStarted(properties);
}

void P2PManager::GroupFinished(const KeyValueStore& properties) {
  RpcIdentifier interface_path = RpcIdentifier("");
  if (properties.Contains<RpcIdentifier>(
          WPASupplicant::kGroupFinishedPropertyInterfaceObject)) {
    interface_path = properties.Get<RpcIdentifier>(
        WPASupplicant::kGroupFinishedPropertyInterfaceObject);
  }
  if (interface_path.value().empty()) {
    LOG(WARNING) << "Ignored " << __func__ << " without interface";
    return;
  }
  auto delegate =
      base::Contains(supplicant_primary_p2pdevice_event_delegates_,
                     interface_path)
          ? supplicant_primary_p2pdevice_event_delegates_[interface_path]
          : nullptr;
  if (!delegate) {
    LOG(ERROR) << "Ignored " << __func__
               << " while not expected, interface: " << interface_path.value();
    return;
  } else {
    supplicant_primary_p2pdevice_event_delegates_.erase(interface_path);
  }

  LOG(INFO) << "Got " << __func__ << ", interface: " << interface_path.value();
  delegate->GroupFinished(properties);
}

void P2PManager::GroupFormationFailure(const std::string& reason) {
  auto delegate = supplicant_primary_p2pdevice_pending_event_delegate_;
  if (!delegate) {
    LOG(WARNING) << "Ignored " << __func__
                 << " while not expected, reason: " << reason;
    return;
  }
  supplicant_primary_p2pdevice_pending_event_delegate_ = nullptr;

  LOG(INFO) << "Got " << __func__ << ", reason: " << reason;
  delegate->GroupFormationFailure(reason);
}

void P2PManager::OnP2PDeviceEvent(LocalDevice::DeviceEvent event,
                                  const LocalDevice* device) {
  if (device->iface_type() != LocalDevice::IfaceType::kP2PGO &&
      device->iface_type() != LocalDevice::IfaceType::kP2PClient) {
    LOG(ERROR) << "Received P2P event from device "
               << device->link_name().value_or("(no link name)")
               << " with invalid type " << device->iface_type();
  }
  bool is_go = device->iface_type() == LocalDevice::IfaceType::kP2PGO;

  // Get the P2PDevice typed reference for the LocalDevice object.
  P2PDeviceRefPtr p2p_dev;
  auto devs = is_go ? p2p_group_owners_ : p2p_clients_;
  for (auto& it : devs) {
    if (it.second == device) {
      p2p_dev = it.second;
    }
  }
  if (!p2p_dev) {
    LOG(ERROR) << "Received event from unmatched P2P device: "
               << device->link_name().value_or("(no link name)");
    return;
  }

  LOG(INFO) << "P2PManager received P2P device "
            << p2p_dev->link_name().value_or("(no link name)")
            << " event: " << event;

  switch (event) {
    case LocalDevice::DeviceEvent::kLinkDown: {
      P2PDevice::P2PDeviceState dev_state = p2p_dev->state();
      DeleteP2PDevice(p2p_dev);
      if (!result_callback_) {
        // kLinkDown should only occur in response to an explicit request,
        // so we should always have an active callback.
        LOG(ERROR) << "No available callback for event: " << event;
        return;
      }
      // TODO(b/323064949): Move all timeout handling logic into P2PManager.
      // When this is implemented, then we'll no longer need to handle timeout
      // events here.
      if (dev_state != P2PDevice::P2PDeviceState::kReady) {
        PostResult(is_go ? kDestroyP2PGroupResultTimeout
                         : kDisconnectFromP2PGroupResultTimeout,
                   std::nullopt, std::move(result_callback_));
        return;
      }
      PostResult(is_go ? kDestroyP2PGroupResultSuccess
                       : kDisconnectFromP2PGroupResultSuccess,
                 std::nullopt, std::move(result_callback_));
      return;
    }
    case LocalDevice::DeviceEvent::kLinkFailure:
      DeleteP2PDevice(p2p_dev);
      supplicant_primary_p2pdevice_pending_event_delegate_ = nullptr;
      if (!result_callback_) {
        return;
      }
      PostResult(is_go ? kCreateP2PGroupResultOperationFailed
                       : kConnectToP2PGroupResultOperationFailed,
                 std::nullopt, std::move(result_callback_));
      return;
    case LocalDevice::DeviceEvent::kInterfaceEnabled:
      OnP2PDeviceEnabled(p2p_dev);
      break;
    case LocalDevice::DeviceEvent::kLinkUp:
      // P2PDevice handles network creation so no action is needed here.
      break;
    case LocalDevice::DeviceEvent::kPeerConnected:
      if (!is_go) {
        LOG(ERROR) << "Received " << event << " event for a P2P Client device.";
        return;
      }
      OnPeerAssoc(p2p_dev);
      break;
    case LocalDevice::DeviceEvent::kPeerDisconnected:
      if (!is_go) {
        LOG(ERROR) << "Received " << event << " event for a P2P Client device.";
        return;
      }
      OnPeerDisassoc(p2p_dev);
      break;
    case LocalDevice::DeviceEvent::kNetworkUp:
      P2PNetworkStarted(p2p_dev);
      break;
    case LocalDevice::DeviceEvent::kInterfaceDisabled:
    case LocalDevice::DeviceEvent::kNetworkDown:
    case LocalDevice::DeviceEvent::kNetworkFailure:
      // TODO(b/295056306): Implement kNetworkDown and kNetworkFailure handling.
      LOG(ERROR) << "Recieved unexpected " << event
                 << " event which has not been implemented.";
      break;
  }
}

void P2PManager::P2PNetworkStarted(P2PDeviceRefPtr device) {
  if (device->iface_type() != LocalDevice::IfaceType::kP2PGO &&
      device->iface_type() != LocalDevice::IfaceType::kP2PClient) {
    LOG(ERROR) << "Received network started on device "
               << device->link_name().value_or("(no link name)")
               << " with invalid type " << device->iface_type();
  }
  manager_->wifi_provider()->RegisterLocalDevice(device);
  std::string result_code =
      device->iface_type() == LocalDevice::IfaceType::kP2PGO
          ? kCreateP2PGroupResultSuccess
          : kConnectToP2PGroupResultSuccess;
  PostResult(result_code, device->shill_id(), std::move(result_callback_));
  return;
}

}  // namespace shill
