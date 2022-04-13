// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_

#include <string>

#include <dbus/object_path.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote_set.h>

#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

// Production implementation of the BluetoothEvents interface.
class BluetoothEventsImpl final : public BluetoothEvents {
 public:
  explicit BluetoothEventsImpl(Context* context);
  BluetoothEventsImpl(const BluetoothEventsImpl&) = delete;
  BluetoothEventsImpl& operator=(const BluetoothEventsImpl&) = delete;
  ~BluetoothEventsImpl() override;

  // BluetoothEvents overrides:
  void AddObserver(mojo::PendingRemote<
                   ash::cros_healthd::mojom::CrosHealthdBluetoothObserver>
                       observer) override;

 private:
  void SetProxyCallback();
  void AdapterAdded(org::bluez::Adapter1ProxyInterface* adapter);
  void AdapterRemoved(const dbus::ObjectPath& adapter_path);
  void AdapterPropertyChanged(org::bluez::Adapter1ProxyInterface* adapter,
                              const std::string& property_name);
  void DeviceAdded(org::bluez::Device1ProxyInterface* device);
  void DeviceRemoved(const dbus::ObjectPath& device_path);
  void DevicePropertyChanged(org::bluez::Device1ProxyInterface* device,
                             const std::string& property_name);

  FRIEND_TEST(BluetoothEventsImplTest, ReceiveAdapterAddedEvent);
  FRIEND_TEST(BluetoothEventsImplTest, ReceiveAdapterRemovedEvent);
  FRIEND_TEST(BluetoothEventsImplTest, ReceiveAdapterPropertyChangedEvent);
  FRIEND_TEST(BluetoothEventsImplTest, ReceiveDeviceAddedEvent);
  FRIEND_TEST(BluetoothEventsImplTest, ReceiveDeviceRemovedEvent);
  FRIEND_TEST(BluetoothEventsImplTest, ReceiveDevicePropertyChangedEvent);

  // Each observer in |observers_| will be notified of any Bluetooth event in
  // the ash::cros_healthd::mojom::CrosHealthdBluetoothObserver interface.
  // The InterfacePtrSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::RemoteSet<ash::cros_healthd::mojom::CrosHealthdBluetoothObserver>
      observers_;

  // Unowned pointer. Should outlive this instance.
  Context* const context_ = nullptr;
  base::WeakPtrFactory<BluetoothEventsImpl> weak_ptr_factory_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_
