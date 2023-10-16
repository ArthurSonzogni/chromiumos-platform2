// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/supplicant_group_proxy.h"

#include "shill/logging.h"
#include "shill/supplicant/supplicant_group_event_delegate_interface.h"
#include "shill/supplicant/wpa_supplicant.h"

#include <base/logging.h>

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static std::string ObjectID(const dbus::ObjectPath* p) {
  return p->value();
}
}  // namespace Logging

SupplicantGroupProxy::PropertySet::PropertySet(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(kPropertyMembers, &members);
  RegisterProperty(kPropertyRole, &role);
  RegisterProperty(kPropertySSID, &ssid);
  RegisterProperty(kPropertyBSSID, &bssid);
  RegisterProperty(kPropertyFrequency, &frequency);
  RegisterProperty(kPropertyPassphrase, &passphrase);
}

SupplicantGroupProxy::SupplicantGroupProxy(
    const scoped_refptr<dbus::Bus>& bus,
    const RpcIdentifier& object_path,
    SupplicantGroupEventDelegateInterface* delegate)
    : group_proxy_(new fi::w1::wpa_supplicant1::GroupProxy(
          bus, WPASupplicant::kDBusAddr, object_path)),
      delegate_(delegate) {
  // Register properties.
  properties_.reset(new PropertySet(
      group_proxy_->GetObjectProxy(), kInterfaceName,
      base::BindRepeating(&SupplicantGroupProxy::OnPropertyChanged,
                          weak_factory_.GetWeakPtr())));

  // Register signal handlers.
  auto on_connected_callback = base::BindRepeating(
      &SupplicantGroupProxy::OnSignalConnected, weak_factory_.GetWeakPtr());
  group_proxy_->RegisterPeerJoinedSignalHandler(
      base::BindRepeating(&SupplicantGroupProxy::PeerJoined,
                          weak_factory_.GetWeakPtr()),
      on_connected_callback);
  group_proxy_->RegisterPeerDisconnectedSignalHandler(
      base::BindRepeating(&SupplicantGroupProxy::PeerDisconnected,
                          weak_factory_.GetWeakPtr()),
      on_connected_callback);

  // Connect property signals and initialize cached values. Based on
  // recommendations from src/dbus/property.h.
  properties_->ConnectSignals();
  properties_->GetAll();
}

SupplicantGroupProxy::~SupplicantGroupProxy() = default;

bool SupplicantGroupProxy::GetMembers(
    std::vector<dbus::ObjectPath>* members) const {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
  CHECK(members);
  if (!properties_->members.GetAndBlock() || !properties_->members.is_valid()) {
    LOG(ERROR) << "Failed to obtain group members";
    return false;
  }
  *members = properties_->members.value();
  return true;
}

bool SupplicantGroupProxy::GetRole(std::string* role) const {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
  CHECK(role);
  if (!properties_->role.GetAndBlock() || !properties_->role.is_valid()) {
    LOG(ERROR) << "Failed to obtain group role";
    return false;
  }
  *role = properties_->role.value();
  return true;
}

bool SupplicantGroupProxy::GetSSID(std::vector<uint8_t>* ssid) const {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
  CHECK(ssid);
  if (!properties_->ssid.GetAndBlock() || !properties_->ssid.is_valid()) {
    LOG(ERROR) << "Failed to obtain group ssid";
    return false;
  }
  *ssid = properties_->ssid.value();
  return true;
}

bool SupplicantGroupProxy::GetBSSID(std::vector<uint8_t>* bssid) const {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
  CHECK(bssid);
  if (!properties_->bssid.GetAndBlock() || !properties_->bssid.is_valid()) {
    LOG(ERROR) << "Failed to obtain group bssid";
    return false;
  }
  *bssid = properties_->bssid.value();
  return true;
}

bool SupplicantGroupProxy::GetFrequency(uint16_t* frequency) const {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
  CHECK(frequency);
  if (!properties_->frequency.GetAndBlock() ||
      !properties_->frequency.is_valid()) {
    LOG(ERROR) << "Failed to obtain group frequency";
    return false;
  }
  *frequency = properties_->frequency.value();
  return true;
}

bool SupplicantGroupProxy::GetPassphrase(std::string* passphrase) const {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
  CHECK(passphrase);
  if (!properties_->passphrase.GetAndBlock() ||
      !properties_->passphrase.is_valid()) {
    LOG(ERROR) << "Failed to obtain group passphrase";
    return false;
  }
  *passphrase = properties_->passphrase.value();
  return true;
}

void SupplicantGroupProxy::PeerJoined(const dbus::ObjectPath& peer) {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
  delegate_->PeerJoined(peer);
}

void SupplicantGroupProxy::PeerDisconnected(const dbus::ObjectPath& peer) {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
  delegate_->PeerDisconnected(peer);
}

void SupplicantGroupProxy::OnPropertyChanged(const std::string& property_name) {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__ << ": " << property_name;
}

void SupplicantGroupProxy::OnSignalConnected(const std::string& interface_name,
                                             const std::string& signal_name,
                                             bool success) {
  SLOG(&group_proxy_->GetObjectPath(), 2)
      << __func__ << ": interface: " << interface_name
      << " signal: " << signal_name << "success: " << success;
  if (!success) {
    LOG(ERROR) << "Failed to connect signal " << signal_name << " to interface "
               << interface_name;
  }
}

}  // namespace shill
