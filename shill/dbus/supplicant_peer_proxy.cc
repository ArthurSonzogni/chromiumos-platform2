// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/supplicant_peer_proxy.h"

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

SupplicantPeerProxy::PropertySet::PropertySet(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(kPropertyDeviceName, &device_name);
  RegisterProperty(kPropertyDeviceCap, &device_cap);
  RegisterProperty(kPropertyGroupCap, &group_cap);
  RegisterProperty(kPropertyDeviceAddress, &device_address);
}

SupplicantPeerProxy::SupplicantPeerProxy(const scoped_refptr<dbus::Bus>& bus,
                                         const RpcIdentifier& object_path)
    : peer_proxy_(new fi::w1::wpa_supplicant1::PeerProxy(
          bus, WPASupplicant::kDBusAddr, object_path)) {
  // Register properties.
  properties_.reset(new PropertySet(
      peer_proxy_->GetObjectProxy(), kInterfaceName,
      base::BindRepeating(&SupplicantPeerProxy::OnPropertyChanged,
                          weak_factory_.GetWeakPtr())));

  // Register signal handlers.
  auto on_connected_callback = base::BindRepeating(
      &SupplicantPeerProxy::OnSignalConnected, weak_factory_.GetWeakPtr());
  peer_proxy_->RegisterPropertiesChangedSignalHandler(
      base::BindRepeating(&SupplicantPeerProxy::PropertiesChanged,
                          weak_factory_.GetWeakPtr()),
      on_connected_callback);
}

SupplicantPeerProxy::~SupplicantPeerProxy() = default;

void SupplicantPeerProxy::PropertiesChanged(
    const brillo::VariantDictionary& /*properties*/) {
  SLOG(&peer_proxy_->GetObjectPath(), 2) << __func__;
}

void SupplicantPeerProxy::OnPropertyChanged(const std::string& property_name) {
  SLOG(&peer_proxy_->GetObjectPath(), 2) << __func__ << ": " << property_name;
}

void SupplicantPeerProxy::OnSignalConnected(const std::string& interface_name,
                                            const std::string& signal_name,
                                            bool success) {
  SLOG(&peer_proxy_->GetObjectPath(), 2)
      << __func__ << ": interface: " << interface_name
      << " signal: " << signal_name << "success: " << success;
  if (!success) {
    LOG(ERROR) << "Failed to connect signal " << signal_name << " to interface "
               << interface_name;
  }
}

}  // namespace shill
