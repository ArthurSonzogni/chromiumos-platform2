// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/floss_event_hub.h"

namespace diagnostics {

FlossEventHub::FlossEventHub(
    org::chromium::bluetooth::ObjectManagerProxy* bluetooth_proxy) {
  if (bluetooth_proxy) {
    bluetooth_proxy->SetBluetoothAddedCallback(base::BindRepeating(
        &FlossEventHub::OnAdapterAdded, weak_ptr_factory_.GetWeakPtr()));
    bluetooth_proxy->SetBluetoothRemovedCallback(base::BindRepeating(
        &FlossEventHub::OnAdapterRemoved, weak_ptr_factory_.GetWeakPtr()));
  }
}

base::CallbackListSubscription FlossEventHub::SubscribeAdapterAdded(
    OnFlossAdapterAddedCallback callback) {
  return adapter_added_observers_.Add(callback);
}

base::CallbackListSubscription FlossEventHub::SubscribeAdapterRemoved(
    OnFlossAdapterRemovedCallback callback) {
  return adapter_removed_observers_.Add(callback);
}

void FlossEventHub::OnAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  adapter_added_observers_.Notify(adapter);
}

void FlossEventHub::OnAdapterRemoved(const dbus::ObjectPath& adapter_path) {
  adapter_removed_observers_.Notify(adapter_path);
}

}  // namespace diagnostics
