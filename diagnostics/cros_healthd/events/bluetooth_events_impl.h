// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_

#include <string>
#include <vector>

#include <base/callback_list.h>
#include <dbus/object_path.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote_set.h>

#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/dbus_bindings/bluez/dbus-proxies.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

enum class BtPropertyType : uint32_t;

// Production implementation of the BluetoothEvents interface.
class BluetoothEventsImpl final : public BluetoothEvents {
 public:
  explicit BluetoothEventsImpl(Context* context);
  BluetoothEventsImpl(const BluetoothEventsImpl&) = delete;
  BluetoothEventsImpl& operator=(const BluetoothEventsImpl&) = delete;
  ~BluetoothEventsImpl() override;

  // BluetoothEvents overrides:
  void AddObserver(mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver>
                       observer) override;

 private:
  void OnBluezAdapterAdded(org::bluez::Adapter1ProxyInterface* adapter);
  void OnBluezAdapterRemoved(const dbus::ObjectPath& adapter_path);
  void OnBluezAdapterPropertyChanged(
      org::bluez::Adapter1ProxyInterface* adapter,
      const std::string& property_name);
  void OnBluezDeviceAdded(org::bluez::Device1ProxyInterface* device);
  void OnBluezDeviceRemoved(const dbus::ObjectPath& device_path);
  void OnBluezDevicePropertyChanged(org::bluez::Device1ProxyInterface* device,
                                    const std::string& property_name);

  void OnFlossAdapterAdded(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter);
  void OnFlossAdapterRemoved(const dbus::ObjectPath& adapter_path);
  void OnFlossAdapterPropertyChanged(const dbus::ObjectPath& adapter_path,
                                     BtPropertyType property);
  void OnFlossAdapterDiscoveringChanged(const dbus::ObjectPath& adapter_path,
                                        bool discovering);
  void OnFlossDeviceAdded(const brillo::VariantDictionary& device);
  void OnFlossDeviceRemoved(const brillo::VariantDictionary& device);
  void OnFlossDevicePropertyChanged(const brillo::VariantDictionary& device,
                                    BtPropertyType property);

  // Each observer in |observers_| will be notified of any Bluetooth event in
  // the ash::cros_healthd::mojom::CrosHealthdBluetoothObserver interface.
  // The InterfacePtrSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::RemoteSet<ash::cros_healthd::mojom::EventObserver> observers_;
  // The callback will be unregistered when the subscription is destructured.
  std::vector<base::CallbackListSubscription> event_subscriptions_;
  // Must be the last member of the class.
  base::WeakPtrFactory<BluetoothEventsImpl> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_
