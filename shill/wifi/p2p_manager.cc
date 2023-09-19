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

#include "shill/error.h"
#include "shill/mac_address.h"
#include "shill/manager.h"
#include "shill/store/property_accessor.h"
#include "shill/wifi/local_device.h"

namespace shill {

P2PManager::P2PManager(Manager* manager)
    : manager_(manager), allowed_(false), next_unique_id_(0) {}

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
  // TODO(b/295050788): it requires wifi phy to have
  // ability to get hardware support for Wifi Direct.
  return true;
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
    caps.Set<Integers>(kP2PCapapabilitiesPreferredChannelsProperty,
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

void P2PManager::Stop() {}

void P2PManager::CreateP2PGroup(
    base::OnceCallback<void(KeyValueStore result)> callback,
    const KeyValueStore& args) {
  LOG(INFO) << __func__;

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

  P2PDeviceRefPtr p2p_dev = manager_->wifi_provider()->CreateP2PDevice(
      LocalDevice::IfaceType::kP2PGO,
      base::BindRepeating(&P2PManager::OnP2PDeviceEvent,
                          base::Unretained(this)),
      next_unique_id_);
  next_unique_id_++;

  if (!p2p_dev) {
    LOG(ERROR) << "Failed to create a WiFi P2P interface.";
    PostResult(kCreateP2PGroupResultOperationFailed, std::nullopt,
               std::move(callback));
    return;
  }
  if (!p2p_dev->SetEnabled(true)) {
    LOG(ERROR) << "Failed to enable a WiFi P2P interface.";
    PostResult(kCreateP2PGroupResultOperationFailed, std::nullopt,
               std::move(callback));
    return;
  }
  std::unique_ptr<P2PService> service =
      std::make_unique<P2PService>(p2p_dev, ssid, passphrase, freq);
  if (!p2p_dev->CreateGroup(std::move(service))) {
    LOG(ERROR) << "Failed to initiate group creation";
    PostResult(kCreateP2PGroupResultOperationFailed, std::nullopt,
               std::move(callback));
    DeleteP2PDevice(p2p_dev);
    return;
  }

  manager_->wifi_provider()->RegisterP2PDevice(p2p_dev);
  p2p_group_owners_[p2p_dev->shill_id()] = p2p_dev;
  PostResult(kCreateP2PGroupResultSuccess, p2p_dev->shill_id(),
             std::move(callback));
}

void P2PManager::ConnectToP2PGroup(
    base::OnceCallback<void(KeyValueStore result)> callback,
    const KeyValueStore& args) {
  LOG(INFO) << __func__;

  if (!args.Contains<std::string>(kP2PDeviceSSID)) {
    LOG(ERROR) << std::string(kP2PDeviceSSID) + " argument is mandatory";
    PostResult(kConnectToP2PGroupResultInvalidArguments, std::nullopt,
               std::move(callback));
    return;
  }
  std::string ssid = args.Get<std::string>(kP2PDeviceSSID);

  if (!args.Contains<std::string>(kP2PDevicePassphrase)) {
    LOG(ERROR) << std::string(kP2PDevicePassphrase) + " argument is mandatory";
    PostResult(kConnectToP2PGroupResultInvalidArguments, std::nullopt,
               std::move(callback));
    return;
  }
  std::string passphrase = args.Get<std::string>(kP2PDevicePassphrase);

  std::optional<uint32_t> freq;
  if (args.Contains<uint32_t>(kP2PDeviceFrequency)) {
    freq = args.Get<uint32_t>(kP2PDeviceFrequency);
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
               std::move(callback));
    return;
  }
  if (!p2p_dev->SetEnabled(true)) {
    LOG(ERROR) << "Failed to enable a WiFi P2P interface.";
    PostResult(kConnectToP2PGroupResultOperationFailed, std::nullopt,
               std::move(callback));
    return;
  }
  std::unique_ptr<P2PService> service =
      std::make_unique<P2PService>(p2p_dev, ssid, passphrase, freq);
  if (!p2p_dev->Connect(std::move(service))) {
    LOG(ERROR) << "Failed to initiate connection";
    PostResult(kConnectToP2PGroupResultOperationFailed, std::nullopt,
               std::move(callback));
    DeleteP2PDevice(p2p_dev);
    return;
  }

  manager_->wifi_provider()->RegisterP2PDevice(p2p_dev);
  p2p_clients_[p2p_dev->shill_id()] = p2p_dev;
  PostResult(kConnectToP2PGroupResultSuccess, p2p_dev->shill_id(),
             std::move(callback));
}

void P2PManager::DestroyP2PGroup(
    base::OnceCallback<void(KeyValueStore result)> callback, int shill_id) {
  LOG(INFO) << __func__;
  if (!base::Contains(p2p_group_owners_, shill_id)) {
    LOG(ERROR) << "There is no P2P client at the requested shill_id: "
               << shill_id;
    PostResult(kDestroyP2PGroupResultNoGroup, std::nullopt,
               std::move(callback));
    return;
  }
  P2PDeviceRefPtr p2p_dev = p2p_group_owners_[shill_id];
  DeleteP2PDevice(p2p_dev);
  PostResult(kDestroyP2PGroupResultSuccess, shill_id, std::move(callback));
}

void P2PManager::DisconnectFromP2PGroup(
    base::OnceCallback<void(KeyValueStore result)> callback, int shill_id) {
  LOG(INFO) << __func__;
  if (p2p_clients_.find(shill_id) == p2p_clients_.end()) {
    LOG(ERROR) << "There is no P2P client at the requested shill_id: "
               << shill_id;
    PostResult(kDisconnectFromP2PGroupResultNotConnected, std::nullopt,
               std::move(callback));
    return;
  }
  P2PDeviceRefPtr p2p_dev = p2p_clients_[shill_id];
  DeleteP2PDevice(p2p_dev);
  PostResult(kDisconnectFromP2PGroupResultSuccess, shill_id,
             std::move(callback));
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

void P2PManager::PostResult(
    std::string result_code,
    std::optional<uint32_t> shill_id,
    base::OnceCallback<void(KeyValueStore result)> callback) {
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
}

}  // namespace shill
