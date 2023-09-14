// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_floss_event_hub.h"

namespace diagnostics {

void FakeFlossEventHub::SendAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  OnAdapterAdded(adapter);
}

void FakeFlossEventHub::SendAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  OnAdapterRemoved(adapter_path);
}

void FakeFlossEventHub::SendManagerAdded(
    org::chromium::bluetooth::ManagerProxyInterface* manager) {
  OnManagerAdded(manager);
}

}  // namespace diagnostics
