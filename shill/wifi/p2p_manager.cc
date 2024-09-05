// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_manager.h"

#include <linux/nl80211.h>

#include <algorithm>
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
#include "shill/wifi/wifi_provider.h"

namespace shill {

P2PManager::P2PManager(Manager* manager)
    : manager_(manager),
      allowed_(false),
      next_unique_id_(0),
      pending_p2p_device_(nullptr),
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
  if (wifi_phys.empty()) {
    return false;
  }

  // TODO(b/353995150): Add multiple WiFi phy support.
  auto phy = wifi_phys.front();
  if (!phy->SupportP2PMode()) {
    return false;
  }

  // Only indicate P2P support if STA/P2P MCC is supported as a STA connection
  // could be attempted or the connected STA interface could attempt a channel
  // switch during an active P2P session.
  uint32_t num_supported_channels = std::min(
      phy->SupportsConcurrency({NL80211_IFTYPE_P2P_GO, NL80211_IFTYPE_STATION}),
      phy->SupportsConcurrency(
          {NL80211_IFTYPE_P2P_CLIENT, NL80211_IFTYPE_STATION}));
  return (num_supported_channels > 1);
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
  auto wifi_phys = manager_->wifi_provider()->GetPhys();
  if (wifi_phys.empty()) {
    return Integers();
  }
  // TODO(b/353995150): Add multiple WiFi phy support.
  auto freqs = wifi_phys.front()->GetFrequencies();
  return std::vector<int>(freqs.begin(), freqs.end());
}

Integers P2PManager::PreferredChannels() {
  auto wifi_phys = manager_->wifi_provider()->GetPhys();
  if (wifi_phys.empty()) {
    return Integers();
  }
  // TODO(b/353995150): Add multiple WiFi phy support.
  auto active_freqs = wifi_phys.front()->GetActiveFrequencies();
  auto supported_freqs = wifi_phys.front()->GetFrequencies();
  std::set<int> freqs;
  // Intersect active frequencies with supported frequencies so that only active
  // frequencies which are also supported by P2P operation are considered as
  // preferred frequencies.
  set_intersection(active_freqs.begin(), active_freqs.end(),
                   supported_freqs.begin(), supported_freqs.end(),
                   inserter(freqs, freqs.begin()));
  return std::vector<int>(freqs.begin(), freqs.end());
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

void P2PManager::Start() {
  LOG(INFO) << __func__;
}

void P2PManager::Stop() {
  LOG(INFO) << __func__;
  if (!p2p_group_owners_.empty() || !p2p_clients_.empty()) {
    LOG(WARNING) << "P2PManager has been stopped while some of P2P devices "
                    "are still active";
    for (auto& it : p2p_group_owners_) {
      DeleteP2PDevice(it.second);
    }
    p2p_group_owners_.clear();
    for (auto& it : p2p_clients_) {
      DeleteP2PDevice(it.second);
    }
    p2p_clients_.clear();
  }
}

void P2PManager::ActionTimerExpired(bool is_start,
                                    LocalDevice::IfaceType iface_type) {
  if (iface_type != LocalDevice::IfaceType::kP2PGO &&
      iface_type != LocalDevice::IfaceType::kP2PClient) {
    LOG(ERROR) << __func__ << ": invalid interface type " << iface_type;
    return;
  }
  bool is_go = iface_type == LocalDevice::IfaceType::kP2PGO;
  LOG(INFO) << __func__ << ": action " << (is_start ? "start" : "stop");
  if (is_start) {
    manager_->wifi_provider()->CancelDeviceRequestsOfType(
        is_go ? NL80211_IFTYPE_P2P_GO : NL80211_IFTYPE_P2P_CLIENT);
  }
  if (!result_callback_) {
    LOG(ERROR) << __func__ << ": no available callback";
    return;
  }
  DeleteP2PDevice(pending_p2p_device_);
  pending_p2p_device_ = nullptr;
  supplicant_primary_p2pdevice_pending_event_delegate_ = nullptr;
  if (is_start) {
    PostResult(
        is_go ? kCreateP2PGroupResultTimeout : kConnectToP2PGroupResultTimeout,
        std::nullopt, std::move(result_callback_));
  } else {
    PostResult(is_go ? kDestroyP2PGroupResultTimeout
                     : kDisconnectFromP2PGroupResultTimeout,
               std::nullopt, std::move(result_callback_));
  }
}

void P2PManager::CancelActionTimer() {
  if (!action_timer_callback_.IsCancelled()) {
    action_timer_callback_.Cancel();
    LOG(INFO) << __func__ << ": action timer cancelled";
  }
  pending_p2p_device_ = nullptr;
}

void P2PManager::SetActionTimer(bool is_start,
                                LocalDevice::IfaceType iface_type) {
  auto timeout = is_start ? (iface_type == LocalDevice::IfaceType::kP2PGO
                                 ? kP2PGOStartTimeout
                                 : kP2PClientStartTimeout)
                          : kP2PStopTimeout;
  CancelActionTimer();
  action_timer_callback_.Reset(base::BindOnce(&P2PManager::ActionTimerExpired,
                                              weak_ptr_factory_.GetWeakPtr(),
                                              is_start, iface_type));
  manager_->dispatcher()->PostDelayedTask(
      FROM_HERE, action_timer_callback_.callback(), timeout);
  LOG(INFO) << __func__ << ": action timer started, timeout: " << timeout;
}

void P2PManager::CreateP2PGroup(P2PResultCallback callback,
                                const KeyValueStore& args) {
  LOG(INFO) << __func__;
  CHECK(callback) << "Callback is empty";

  if (supplicant_primary_p2pdevice_pending_event_delegate_ ||
      result_callback_ || !action_timer_callback_.IsCancelled()) {
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

  std::optional<int32_t> freq;
  if (args.Contains<int32_t>(kP2PDeviceFrequency)) {
    freq = args.Get<int32_t>(kP2PDeviceFrequency);
    auto freqs = SupportedChannels();
    if (std::count(freqs.begin(), freqs.end(), freq) == 0) {
      LOG(WARNING) << __func__ << ": invalid frequency " << freq.value()
                   << " , reset to null";
      freq = std::nullopt;
    } else {
      LOG(INFO) << __func__ << ": on frequency " << freq.value();
    }
  }

  if (!args.Contains<int32_t>(kP2PDevicePriority)) {
    LOG(ERROR) << std::string(kP2PDevicePriority) + " argument is mandatory";
    PostResult(kConnectToP2PGroupResultInvalidArguments, std::nullopt,
               std::move(result_callback_));
    return;
  }
  WiFiPhy::Priority priority =
      WiFiPhy::Priority(args.Get<int32_t>(kP2PDevicePriority));
  if (!priority.IsValid()) {
    LOG(ERROR) << "invalid " << std::string(kP2PDevicePriority) + " argument "
               << priority;
    PostResult(kConnectToP2PGroupResultInvalidArguments, std::nullopt,
               std::move(result_callback_));
    return;
  }

  if (!ConnectToSupplicantPrimaryP2PDeviceProxy()) {
    LOG(ERROR) << "Failed to create P2P group, primary P2PDevice proxy "
                  "is not connected";
    PostResult(kCreateP2PGroupResultOperationFailed, std::nullopt,
               std::move(result_callback_));
    return;
  }

  base::OnceCallback<void(P2PDeviceRefPtr)> success_cb =
      base::BindOnce(&P2PManager::OnDeviceCreated, base::Unretained(this),
                     LocalDevice::IfaceType::kP2PGO, ssid, passphrase, freq);
  base::OnceCallback<void()> fail_cb =
      base::BindOnce(&P2PManager::OnDeviceCreationFailed,
                     base::Unretained(this), LocalDevice::IfaceType::kP2PGO);
  LocalDevice::EventCallback event_cb = base::BindRepeating(
      &P2PManager::OnP2PDeviceEvent, base::Unretained(this));

  base::OnceClosure create_device_cb = base::BindOnce(
      &WiFiProvider::CreateP2PDevice, manager_->wifi_provider()->AsWeakPtr(),
      LocalDevice::IfaceType::kP2PGO, event_cb, next_unique_id_, priority,
      std::move(success_cb), std::move(fail_cb));

  // Arm the start timer before sending the device creation request.
  SetActionTimer(true, LocalDevice::IfaceType::kP2PGO);
  bool request_accepted = manager_->wifi_provider()->RequestLocalDeviceCreation(
      LocalDevice::IfaceType::kP2PGO, priority, std::move(create_device_cb));
  next_unique_id_++;
  if (!request_accepted) {
    LOG(INFO)
        << "Failed to create a WiFi P2P interface due to concurrency conflict.";
    CancelActionTimerAndPostResult(kCreateP2PGroupResultConcurrencyNotSupported,
                                   std::nullopt);
    DisconnectFromSupplicantPrimaryP2PDeviceProxy();
    return;
  }
}

void P2PManager::ConnectToP2PGroup(P2PResultCallback callback,
                                   const KeyValueStore& args) {
  LOG(INFO) << __func__;
  CHECK(callback) << "Callback is empty";

  if (supplicant_primary_p2pdevice_pending_event_delegate_ ||
      result_callback_ || !action_timer_callback_.IsCancelled()) {
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

  std::optional<int32_t> freq;
  if (args.Contains<int32_t>(kP2PDeviceFrequency)) {
    freq = args.Get<int32_t>(kP2PDeviceFrequency);
    auto freqs = SupportedChannels();
    if (std::count(freqs.begin(), freqs.end(), freq) == 0) {
      LOG(WARNING) << __func__ << ": invalid frequency " << freq.value()
                   << " , reset to null";
      freq = std::nullopt;
    } else {
      LOG(INFO) << __func__ << ": on frequency " << freq.value();
    }
  }

  if (!args.Contains<int32_t>(kP2PDevicePriority)) {
    LOG(ERROR) << std::string(kP2PDevicePriority) + " argument is mandatory";
    PostResult(kConnectToP2PGroupResultInvalidArguments, std::nullopt,
               std::move(result_callback_));
    return;
  }
  WiFiPhy::Priority priority =
      WiFiPhy::Priority(args.Get<int32_t>(kP2PDevicePriority));
  if (!priority.IsValid()) {
    LOG(ERROR) << "invalid " << std::string(kP2PDevicePriority) + " argument "
               << priority;
    PostResult(kConnectToP2PGroupResultInvalidArguments, std::nullopt,
               std::move(result_callback_));
    return;
  }

  if (!ConnectToSupplicantPrimaryP2PDeviceProxy()) {
    LOG(ERROR) << "Failed to connect to P2P group, primary P2PDevice proxy "
                  "is not connected";
    PostResult(kConnectToP2PGroupResultOperationFailed, std::nullopt,
               std::move(result_callback_));
    return;
  }

  base::OnceCallback<void(P2PDeviceRefPtr)> success_cb = base::BindOnce(
      &P2PManager::OnDeviceCreated, base::Unretained(this),
      LocalDevice::IfaceType::kP2PClient, ssid, passphrase, freq);
  base::OnceCallback<void()> fail_cb = base::BindOnce(
      &P2PManager::OnDeviceCreationFailed, base::Unretained(this),
      LocalDevice::IfaceType::kP2PClient);
  LocalDevice::EventCallback event_cb = base::BindRepeating(
      &P2PManager::OnP2PDeviceEvent, base::Unretained(this));

  base::OnceClosure create_device_cb = base::BindOnce(
      &WiFiProvider::CreateP2PDevice, manager_->wifi_provider()->AsWeakPtr(),
      LocalDevice::IfaceType::kP2PClient, event_cb, next_unique_id_, priority,
      std::move(success_cb), std::move(fail_cb));

  // Arm the start timer before sending the device creation request.
  SetActionTimer(true, LocalDevice::IfaceType::kP2PClient);
  bool request_accepted = manager_->wifi_provider()->RequestLocalDeviceCreation(
      LocalDevice::IfaceType::kP2PClient, priority,
      std::move(create_device_cb));
  next_unique_id_++;
  if (!request_accepted) {
    LOG(INFO)
        << "Failed to create a WiFi P2P interface due to concurrency conflict.";
    CancelActionTimerAndPostResult(
        kConnectToP2PGroupResultConcurrencyNotSupported, std::nullopt);
    DisconnectFromSupplicantPrimaryP2PDeviceProxy();
    return;
  }
}

void P2PManager::DestroyP2PGroup(P2PResultCallback callback, int32_t shill_id) {
  LOG(INFO) << __func__;
  CHECK(callback) << "Callback is empty";

  if (result_callback_ || !action_timer_callback_.IsCancelled()) {
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
  SetActionTimer(false, LocalDevice::IfaceType::kP2PClient);
  p2p_group_owners_[shill_id]->RemoveGroup(false);
}

void P2PManager::DisconnectFromP2PGroup(P2PResultCallback callback,
                                        int32_t shill_id) {
  LOG(INFO) << __func__;
  CHECK(callback) << "Callback is empty";

  if (result_callback_ || !action_timer_callback_.IsCancelled()) {
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
  SetActionTimer(false, LocalDevice::IfaceType::kP2PClient);
  p2p_clients_[shill_id]->Disconnect(false);
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
                            std::optional<int32_t> shill_id,
                            P2PResultCallback callback) {
  if (!callback) {
    LOG(ERROR) << "Callback is not set";
    return;
  }
  LOG(INFO) << __func__ << ": " << result_code;
  KeyValueStore response_dict;
  response_dict.Set<std::string>(kP2PResultCode, result_code);
  if (shill_id) {
    response_dict.Set<int32_t>(kP2PDeviceShillID, *shill_id);
  }
  manager_->dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), response_dict));
}

void P2PManager::CancelActionTimerAndPostResult(
    std::string result_code, std::optional<int32_t> shill_id) {
  CancelActionTimer();
  PostResult(result_code, shill_id, std::move(result_callback_));
}

void P2PManager::DeleteP2PDevice(P2PDeviceRefPtr p2p_dev) {
  if (!p2p_dev)
    return;

  if (p2p_dev->iface_type() == LocalDevice::IfaceType::kP2PGO) {
    p2p_group_owners_.erase(p2p_dev->shill_id());
  } else {
    p2p_clients_.erase(p2p_dev->shill_id());
  }
  manager_->wifi_provider()->DeleteLocalDevice(p2p_dev);

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
    return;
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
      DeleteP2PDevice(p2p_dev);
      if (!result_callback_ || action_timer_callback_.IsCancelled()) {
        // If we aren't processing a Shill initiated request, kLinkDown should
        // only occur in response to an explicit stop request, so we should
        // always have an active callback and timer.
        LOG(ERROR) << "No available callback or action timer for event: "
                   << event;
        return;
      }
      CancelActionTimerAndPostResult(is_go
                                         ? kDestroyP2PGroupResultSuccess
                                         : kDisconnectFromP2PGroupResultSuccess,
                                     std::nullopt);
      return;
    }
    case LocalDevice::DeviceEvent::kLinkDownOnResourceBusy: {
      DeleteP2PDevice(p2p_dev);
      return;
    }
    case LocalDevice::DeviceEvent::kLinkFailure:
      DeleteP2PDevice(p2p_dev);
      supplicant_primary_p2pdevice_pending_event_delegate_ = nullptr;
      if (!result_callback_) {
        return;
      }
      CancelActionTimerAndPostResult(
          is_go ? kCreateP2PGroupResultOperationFailed
                : kConnectToP2PGroupResultOperationFailed,
          std::nullopt);
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

void P2PManager::OnDeviceCreated(LocalDevice::IfaceType iface_type,
                                 std::optional<std::string> ssid,
                                 std::optional<std::string> passphrase,
                                 std::optional<int32_t> freq,
                                 P2PDeviceRefPtr device) {
  if (!result_callback_) {
    LOG(ERROR) << "P2PDevice was created with no pending callback.";
    return;
  }

  if (iface_type != device->iface_type()) {
    LOG(ERROR) << "P2PDevice created with type " << device->iface_type()
               << " which does not match requested type " << iface_type;
    return;
  }

  if (device->iface_type() != LocalDevice::IfaceType::kP2PGO &&
      device->iface_type() != LocalDevice::IfaceType::kP2PClient) {
    LOG(ERROR) << "P2PDevice created "
               << device->link_name().value_or("(no link name)")
               << " with invalid type " << device->iface_type();
    return;
  }
  bool is_go = device->iface_type() == LocalDevice::IfaceType::kP2PGO;

  if (!device) {
    LOG(ERROR) << "Failed to create a WiFi P2P interface.";
    CancelActionTimerAndPostResult(
        is_go ? kCreateP2PGroupResultOperationFailed
              : kConnectToP2PGroupResultOperationFailed,
        std::nullopt);
    DisconnectFromSupplicantPrimaryP2PDeviceProxy();
    return;
  }
  if (!device->SetEnabled(true)) {
    LOG(ERROR) << "Failed to enable a WiFi P2P interface.";
    CancelActionTimerAndPostResult(
        is_go ? kCreateP2PGroupResultOperationFailed
              : kConnectToP2PGroupResultOperationFailed,
        std::nullopt);
    DisconnectFromSupplicantPrimaryP2PDeviceProxy();
    return;
  }

  std::unique_ptr<P2PService> service =
      std::make_unique<P2PService>(device, ssid, passphrase, freq);
  pending_p2p_device_ = device;
  if (is_go) {
    p2p_group_owners_[device->shill_id()] = device;
    if (!device->CreateGroup(std::move(service))) {
      LOG(ERROR) << "Failed to initiate group creation";
      CancelActionTimerAndPostResult(kCreateP2PGroupResultOperationFailed,
                                     std::nullopt);
      DeleteP2PDevice(device);
      return;
    }
  } else {
    p2p_clients_[device->shill_id()] = device;
    if (!device->Connect(std::move(service))) {
      LOG(ERROR) << "Failed to initiate connection";
      CancelActionTimerAndPostResult(kConnectToP2PGroupResultOperationFailed,
                                     std::nullopt);
      DeleteP2PDevice(device);
      return;
    }
  }
  supplicant_primary_p2pdevice_pending_event_delegate_ = device.get();
}

void P2PManager::OnDeviceCreationFailed(LocalDevice::IfaceType iface_type) {
  if (!result_callback_) {
    LOG(ERROR) << "P2PDevice was created with no pending callback.";
    return;
  }

  if (iface_type != LocalDevice::IfaceType::kP2PGO &&
      iface_type != LocalDevice::IfaceType::kP2PClient) {
    LOG(ERROR) << "Received DeviceCreationFailed event for invalid type "
               << iface_type;
  }

  bool is_go = iface_type == LocalDevice::IfaceType::kP2PGO;
  LOG(ERROR) << "Failed create P2PDevice.";
  CancelActionTimerAndPostResult(is_go
                                     ? kCreateP2PGroupResultOperationFailed
                                     : kConnectToP2PGroupResultOperationFailed,
                                 std::nullopt);
  DisconnectFromSupplicantPrimaryP2PDeviceProxy();
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
  CancelActionTimerAndPostResult(result_code, device->shill_id());
  return;
}

void P2PManager::DeviceTeardownOnResourceBusy(uint32_t shill_id) {
  if (p2p_group_owners_.contains(shill_id)) {
    p2p_group_owners_[shill_id]->RemoveGroup(true);
    return;
  }
  if (p2p_clients_.contains(shill_id)) {
    p2p_clients_[shill_id]->Disconnect(true);
    return;
  }
}

}  // namespace shill
