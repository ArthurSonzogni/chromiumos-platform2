// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_FLOSS_EVENT_HUB_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_FLOSS_EVENT_HUB_H_

#include <string>
#include <vector>

#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/system/floss_event_hub.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"

namespace diagnostics {

class FakeFlossEventHub final : public FlossEventHub {
 public:
  FakeFlossEventHub() = default;
  FakeFlossEventHub(const FakeFlossEventHub&) = delete;
  FakeFlossEventHub& operator=(const FakeFlossEventHub&) = delete;

  // Send fake events.
  void SendAdapterAdded(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter = nullptr);
  void SendAdapterRemoved(const dbus::ObjectPath& adapter_path);
  void SendAdapterPropertyChanged(const dbus::ObjectPath& adapter_path,
                                  uint32_t property);
  void SendAdapterPoweredChanged(int32_t hci_interface, bool powered);
  void SendAdapterDiscoveringChanged(const dbus::ObjectPath& adapter_path,
                                     bool discovering);
  void SendDeviceAdded(const brillo::VariantDictionary& device);
  void SendDeviceRemoved(const brillo::VariantDictionary& device);
  void SendDevicePropertiesChanged(const brillo::VariantDictionary& device,
                                   const std::vector<uint32_t>& properties);
  void SendManagerAdded(
      org::chromium::bluetooth::ManagerProxyInterface* manager);
  void SendManagerRemoved(
      const dbus::ObjectPath& manager_path = dbus::ObjectPath(""));
  void SendAdapterGattAdded(
      org::chromium::bluetooth::BluetoothGattProxyInterface* adapter_gatt);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_FLOSS_EVENT_HUB_H_
