// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/chromeos_supplicant_network_proxy.h"

#include <string>

#include <base/bind.h>

#include "shill/logging.h"
#include "shill/supplicant/wpa_supplicant.h"

using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static string ObjectID(const dbus::ObjectPath* p) { return p->value(); }
}

// static.
const char ChromeosSupplicantNetworkProxy::kPropertyEnabled[] = "Enabled";
const char ChromeosSupplicantNetworkProxy::kPropertyProperties[] =
    "Properties";

ChromeosSupplicantNetworkProxy::PropertySet::PropertySet(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(kPropertyEnabled, &enabled);
  RegisterProperty(kPropertyProperties, &properties);
}

ChromeosSupplicantNetworkProxy::ChromeosSupplicantNetworkProxy(
    const scoped_refptr<dbus::Bus>& bus,
    const std::string& object_path)
    : network_proxy_(
        new fi::w1::wpa_supplicant1::NetworkProxy(
            bus,
            WPASupplicant::kDBusAddr,
            dbus::ObjectPath(object_path))),
      properties_(
          new PropertySet(
              network_proxy_->GetObjectProxy(),
              WPASupplicant::kDBusAddr,
              base::Bind(&ChromeosSupplicantNetworkProxy::OnPropertyChanged,
                         weak_factory_.GetWeakPtr()))) {
  // Register signal handler.
  network_proxy_->RegisterPropertiesChangedSignalHandler(
      base::Bind(&ChromeosSupplicantNetworkProxy::PropertiesChanged,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ChromeosSupplicantNetworkProxy::OnSignalConnected,
                 weak_factory_.GetWeakPtr()));

  // Connect property signals and initialize cached values. Based on
  // recommendations from src/dbus/property.h.
  properties_->ConnectSignals();
  properties_->GetAll();
}

ChromeosSupplicantNetworkProxy::~ChromeosSupplicantNetworkProxy() {}

bool ChromeosSupplicantNetworkProxy::SetEnabled(bool enabled) {
  SLOG(&network_proxy_->GetObjectPath(), 2) << __func__;
  properties_->enabled.Set(
      enabled, base::Bind(&ChromeosSupplicantNetworkProxy::OnEnabledSet,
                          weak_factory_.GetWeakPtr()));
  return true;
}

void ChromeosSupplicantNetworkProxy::PropertiesChanged(
    const chromeos::VariantDictionary& /*properties*/) {
  SLOG(&network_proxy_->GetObjectPath(), 2) << __func__;
}

void ChromeosSupplicantNetworkProxy::OnPropertyChanged(
    const std::string& property_name) {
  SLOG(&network_proxy_->GetObjectPath(), 2) << __func__ << ": "
      << property_name;
}

void ChromeosSupplicantNetworkProxy::OnEnabledSet(bool success) {
  SLOG(&network_proxy_->GetObjectPath(), 2) << __func__ << ": " << success;
  if (!success) {
    LOG(ERROR) << "Failed to set Enabled property";
  }
}

// Called when signal is connected to the ObjectProxy.
void ChromeosSupplicantNetworkProxy::OnSignalConnected(
    const std::string& interface_name,
    const std::string& signal_name,
    bool success) {
  SLOG(&network_proxy_->GetObjectPath(), 2) << __func__
      << "interface: " << interface_name << " signal: " << signal_name
      << "success: " << success;
  if (!success) {
    LOG(ERROR) << "Failed to connect signal " << signal_name
        << " to interface " << interface_name;
  }
}

}  // namespace shill
