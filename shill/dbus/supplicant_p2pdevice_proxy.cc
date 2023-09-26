// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/supplicant_p2pdevice_proxy.h"

#include "shill/logging.h"
#include "shill/supplicant/supplicant_p2pdevice_event_delegate_interface.h"
#include "shill/supplicant/wpa_supplicant.h"

#include <base/logging.h>

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static std::string ObjectID(const dbus::ObjectPath* p) {
  return p->value();
}
}  // namespace Logging

SupplicantP2PDeviceProxy::PropertySet::PropertySet(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(kPropertyDeviceConfig, &device_config);
}

SupplicantP2PDeviceProxy::SupplicantP2PDeviceProxy(
    const scoped_refptr<dbus::Bus>& bus,
    const RpcIdentifier& object_path,
    SupplicantP2PDeviceEventDelegateInterface* delegate)
    : p2pdevice_proxy_(new fi::w1::wpa_supplicant1::Interface::P2PDeviceProxy(
          bus, WPASupplicant::kDBusAddr, object_path)),
      delegate_(delegate) {
  // Register properties.
  properties_.reset(new PropertySet(
      p2pdevice_proxy_->GetObjectProxy(), kInterfaceName,
      base::BindRepeating(&SupplicantP2PDeviceProxy::OnPropertyChanged,
                          weak_factory_.GetWeakPtr())));

  // Register signal handlers.
  auto on_connected_callback = base::BindRepeating(
      &SupplicantP2PDeviceProxy::OnSignalConnected, weak_factory_.GetWeakPtr());
  p2pdevice_proxy_->RegisterGroupStartedSignalHandler(
      base::BindRepeating(&SupplicantP2PDeviceProxy::GroupStarted,
                          weak_factory_.GetWeakPtr()),
      on_connected_callback);
  p2pdevice_proxy_->RegisterGroupFinishedSignalHandler(
      base::BindRepeating(&SupplicantP2PDeviceProxy::GroupFinished,
                          weak_factory_.GetWeakPtr()),
      on_connected_callback);
  p2pdevice_proxy_->RegisterGroupFormationFailureSignalHandler(
      base::BindRepeating(&SupplicantP2PDeviceProxy::GroupFormationFailure,
                          weak_factory_.GetWeakPtr()),
      on_connected_callback);
}

SupplicantP2PDeviceProxy::~SupplicantP2PDeviceProxy() = default;

bool SupplicantP2PDeviceProxy::GroupAdd(const KeyValueStore& args) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2) << __func__;
  brillo::VariantDictionary dict =
      KeyValueStore::ConvertToVariantDictionary(args);
  brillo::ErrorPtr error;
  if (!p2pdevice_proxy_->GroupAdd(dict, &error)) {
    LOG(INFO) << "Failed to add group: " << error->GetCode() << " "
              << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantP2PDeviceProxy::Disconnect() {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!p2pdevice_proxy_->Disconnect(&error)) {
    LOG(INFO) << "Failed to disconnect from group: " << error->GetCode() << " "
              << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantP2PDeviceProxy::AddPersistentGroup(
    const KeyValueStore& args, RpcIdentifier* rpc_identifier) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2) << __func__;
  brillo::VariantDictionary dict =
      KeyValueStore::ConvertToVariantDictionary(args);
  dbus::ObjectPath path;
  brillo::ErrorPtr error;
  if (!p2pdevice_proxy_->AddPersistentGroup(dict, &path, &error)) {
    LOG(INFO) << "Failed to add persistent group: " << error->GetCode() << " "
              << error->GetMessage();
    return false;
  }
  *rpc_identifier = path;
  return true;
}

bool SupplicantP2PDeviceProxy::RemovePersistentGroup(
    const RpcIdentifier& rpc_identifier) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2)
      << __func__ << ": " << rpc_identifier.value();
  brillo::ErrorPtr error;
  if (!p2pdevice_proxy_->RemovePersistentGroup(rpc_identifier, &error)) {
    LOG(INFO) << "Failed to remove persistent group " << rpc_identifier.value()
              << ": " << error->GetCode() << " " << error->GetMessage();
    return false;
  }
  return true;
}

void SupplicantP2PDeviceProxy::GroupStarted(
    const brillo::VariantDictionary& properties) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2) << __func__;
  KeyValueStore store = KeyValueStore::ConvertFromVariantDictionary(properties);
  delegate_->GroupStarted(store);
}

void SupplicantP2PDeviceProxy::GroupFinished(
    const brillo::VariantDictionary& properties) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2) << __func__;
  KeyValueStore store = KeyValueStore::ConvertFromVariantDictionary(properties);
  delegate_->GroupFinished(store);
}

void SupplicantP2PDeviceProxy::GroupFormationFailure(
    const std::string& reason) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2) << __func__;
  delegate_->GroupFormationFailure(reason);
}

bool SupplicantP2PDeviceProxy::GetDeviceConfig(KeyValueStore* config) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2) << __func__;
  CHECK(config);

  if (!properties_->device_config.GetAndBlock() ||
      !properties_->device_config.is_valid()) {
    LOG(ERROR) << "Failed to obtain P2P device config";
    return false;
  }

  *config = KeyValueStore::ConvertFromVariantDictionary(
      properties_->device_config.value());

  return true;
}

void SupplicantP2PDeviceProxy::OnPropertyChanged(
    const std::string& property_name) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2)
      << __func__ << ": " << property_name;
}

void SupplicantP2PDeviceProxy::OnSignalConnected(
    const std::string& interface_name,
    const std::string& signal_name,
    bool success) {
  SLOG(&p2pdevice_proxy_->GetObjectPath(), 2)
      << __func__ << ": interface: " << interface_name
      << " signal: " << signal_name << "success: " << success;
  if (!success) {
    LOG(ERROR) << "Failed to connect signal " << signal_name << " to interface "
               << interface_name;
  }
}

}  // namespace shill
