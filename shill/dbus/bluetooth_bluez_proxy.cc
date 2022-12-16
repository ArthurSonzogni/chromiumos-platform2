// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/bluetooth_bluez_proxy.h"

#include <memory>
#include <string>

#include <chromeos/dbus/bluetooth/dbus-constants.h>
#include <dbus/object_path.h>

#include "shill/logging.h"
#include "shill/scope_logger.h"

namespace shill {

namespace {
constexpr char kBlueZObjectPath[] = "/org/bluez/hci0";
}

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
}  // namespace Logging

BluetoothBlueZProxy::BluetoothBlueZProxy(const scoped_refptr<dbus::Bus>& bus)
    : bluez_proxy_(new org::bluez::Adapter1Proxy(
          bus,
          bluetooth_adapter::kBluetoothAdapterServiceName,
          dbus::ObjectPath(kBlueZObjectPath))) {
  bluez_proxy_->InitializeProperties(base::BindRepeating(
      &BluetoothBlueZProxy::OnPropertyChanged, weak_factory_.GetWeakPtr()));
}

bool BluetoothBlueZProxy::GetAdapterPowered(bool* powered) const {
  if (!bluez_proxy_->GetProperties()->GetAndBlock(
          &bluez_proxy_->GetProperties()->powered)) {
    LOG(ERROR) << "Failed to query BT 'Powered' property";
    return false;
  }
  if (!bluez_proxy_->is_powered_valid()) {
    LOG(ERROR) << "Invalid BT 'Powered' property";
    return false;
  }
  *powered = bluez_proxy_->powered();
  SLOG(3) << __func__ << ": " << bluez_proxy_->GetObjectPath().value()
          << ": BlueZ BT adapter is "
          << (bluez_proxy_->powered() ? "enabled" : "disabled");
  return true;
}

void BluetoothBlueZProxy::OnPropertyChanged(
    org::bluez::Adapter1ProxyInterface* /* proxy_interface */,
    const std::string& /* property_name */) {}

}  // namespace shill
