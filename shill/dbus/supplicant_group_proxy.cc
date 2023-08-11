// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/supplicant_group_proxy.h"

#include "shill/logging.h"
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

SupplicantGroupProxy::SupplicantGroupProxy(const scoped_refptr<dbus::Bus>& bus,
                                           const RpcIdentifier& object_path)
    : group_proxy_(new fi::w1::wpa_supplicant1::GroupProxy(
          bus, WPASupplicant::kDBusAddr, object_path)) {
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
}

SupplicantGroupProxy::~SupplicantGroupProxy() = default;

void SupplicantGroupProxy::PeerJoined(const dbus::ObjectPath& peer) {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
}

void SupplicantGroupProxy::PeerDisconnected(const dbus::ObjectPath& peer) {
  SLOG(&group_proxy_->GetObjectPath(), 2) << __func__;
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
