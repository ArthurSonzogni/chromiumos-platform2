// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/containers/contains.h>

#include "hermes/modem_manager_proxy.h"

namespace hermes {

ModemManagerProxy::ModemManagerProxy(const scoped_refptr<dbus::Bus>& bus)
    : bus_(bus),
      proxy_(std::make_unique<org::freedesktop::DBus::ObjectManagerProxy>(
          bus,
          modemmanager::kModemManager1ServiceName,
          dbus::ObjectPath(modemmanager::kModemManager1ServicePath))),
      weak_factory_(this) {
  auto on_interface_added = base::BindRepeating(
      &ModemManagerProxy::OnInterfaceAdded, weak_factory_.GetWeakPtr());
  auto on_dbus_signal_connected =
      base::BindOnce([](const std::string& interface, const std::string& signal,
                        bool success) {
        if (!success)
          LOG(ERROR) << "Failed to connect to signal " << interface << "."
                     << signal;
      });
  proxy_->RegisterInterfacesAddedSignalHandler(
      std::move(on_interface_added), std::move(on_dbus_signal_connected));
}

ModemManagerProxy::ModemManagerProxy() : weak_factory_(this) {}

void ModemManagerProxy::RegisterModemAppearedCallback(base::OnceClosure cb) {
  on_modem_appeared_cb_ = std::move(cb);
}

void ModemManagerProxy::WaitForModem(base::OnceClosure cb) {
  VLOG(2) << __func__;
  auto on_service_available =
      base::BindOnce(&ModemManagerProxy::WaitForModemStepGetObjects,
                     weak_factory_.GetWeakPtr(), std::move(cb));
  proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      std::move((on_service_available)));
}

void ModemManagerProxy::WaitForModemStepGetObjects(base::OnceClosure cb, bool) {
  VLOG(2) << __func__;
  auto error_cb = base::BindOnce([](brillo::Error* err) {
    LOG(ERROR) << "Could not get MM managed objects: " << err->GetDomain()
               << ": " << err->GetCode() << ": " << err->GetMessage();
  });
  proxy_->GetManagedObjectsAsync(
      base::BindOnce(&ModemManagerProxy::WaitForModemStepLast,
                     weak_factory_.GetWeakPtr(), std::move(cb)),
      std::move(error_cb));
}

void ModemManagerProxy::WaitForModemStepLast(
    base::OnceClosure cb,
    const DBusObjectsWithProperties& dbus_objects_with_properties) {
  VLOG(2) << __func__;
  for (const auto& object_properties_pair : dbus_objects_with_properties) {
    VLOG(2) << __func__ << ": " << object_properties_pair.first.value();
    if (!base::Contains(object_properties_pair.second,
                        modemmanager::kModemManager1ModemInterface)) {
      continue;
    }
    LOG(INFO) << __func__ << ": Found " << object_properties_pair.first.value();
    RegisterModemAppearedCallback(std::move(cb));
    OnNewModemDetected(dbus::ObjectPath{object_properties_pair.first.value()});
    return;
  }
  LOG(INFO) << __func__ << ": Waiting for modem...";
  RegisterModemAppearedCallback(std::move(cb));
}

void ModemManagerProxy::OnInterfaceAdded(
    const dbus::ObjectPath& object_path,
    const DBusInterfaceToProperties& properties) {
  brillo::ErrorPtr error;
  VLOG(2) << __func__ << ": " << object_path.value();
  if (!base::Contains(properties, modemmanager::kModemManager1ModemInterface)) {
    VLOG(2) << __func__ << "Interfaces added, but not modem interface.";
    return;
  }
  OnNewModemDetected(object_path);
}

void ModemManagerProxy::OnNewModemDetected(dbus::ObjectPath object_path) {
  LOG(INFO) << __func__ << ": New modem detected at " << object_path.value();
  modem_proxy_ = std::make_unique<org::freedesktop::ModemManager1::ModemProxy>(
      bus_, modemmanager::kModemManager1ServiceName, object_path);
  modem_proxy_->InitializeProperties(base::BindRepeating(
      &ModemManagerProxy::OnPropertiesChanged, weak_factory_.GetWeakPtr()));
}

void ModemManagerProxy::OnPropertiesChanged(
    org::freedesktop::ModemManager1::ModemProxyInterface* /*unused*/,
    const std::string& prop) {
  VLOG(2) << __func__ << " : " << prop << " changed.";
  if (!modem_proxy_->GetProperties()->primary_port.is_valid() ||
      !modem_proxy_->GetProperties()->device_identifier.is_valid())
    return;
  if (!on_modem_appeared_cb_.is_null())
    std::move(on_modem_appeared_cb_).Run();
}

std::string ModemManagerProxy::GetPrimaryPort() const {
  return modem_proxy_->primary_port();
}

}  // namespace hermes
