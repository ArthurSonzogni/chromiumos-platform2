// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_

#include <map>
#include <memory>

#include <base/callback_list.h>
#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/system/floss_callback_services.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"

namespace diagnostics {

using OnFlossAdapterAddedCallback = base::RepeatingCallback<void(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter)>;
using OnFlossAdapterRemovedCallback =
    base::RepeatingCallback<void(const dbus::ObjectPath& adapter_path)>;
using OnFlossAdapterPoweredChangedCallback =
    base::RepeatingCallback<void(int32_t hci_interface, bool powered)>;
using OnFlossAdapterDiscoveringChangedCallback = base::RepeatingCallback<void(
    const dbus::ObjectPath& adapter_path, bool discovering)>;
using OnFlossDeviceAddedCallback =
    base::RepeatingCallback<void(const brillo::VariantDictionary& device)>;
using OnFlossDeviceRemovedCallback =
    base::RepeatingCallback<void(const brillo::VariantDictionary& device)>;
using OnFlossManagerRemovedCallback =
    base::RepeatingCallback<void(const dbus::ObjectPath& manager_path)>;

// Interface for subscribing Bluetooth events via Floss proxies.
class FlossEventHub {
 public:
  explicit FlossEventHub(
      const scoped_refptr<dbus::Bus>& bus = nullptr,
      org::chromium::bluetooth::Manager::ObjectManagerProxy*
          bluetooth_manager_proxy = nullptr,
      org::chromium::bluetooth::ObjectManagerProxy* bluetooth_proxy = nullptr);
  FlossEventHub(const FlossEventHub&) = delete;
  FlossEventHub& operator=(const FlossEventHub&) = delete;
  ~FlossEventHub() = default;

  base::CallbackListSubscription SubscribeAdapterAdded(
      OnFlossAdapterAddedCallback callback);
  base::CallbackListSubscription SubscribeAdapterRemoved(
      OnFlossAdapterRemovedCallback callback);
  base::CallbackListSubscription SubscribeAdapterPoweredChanged(
      OnFlossAdapterPoweredChangedCallback callback);
  base::CallbackListSubscription SubscribeAdapterDiscoveringChanged(
      OnFlossAdapterDiscoveringChangedCallback callback);
  base::CallbackListSubscription SubscribeDeviceAdded(
      OnFlossDeviceAddedCallback callback);
  base::CallbackListSubscription SubscribeDeviceRemoved(
      OnFlossDeviceRemovedCallback callback);
  base::CallbackListSubscription SubscribeManagerRemoved(
      OnFlossManagerRemovedCallback callback);

  // TODO(b/300239296): Support adapter and device property changed events.

 protected:
  // Interfaces for subclass to send events.
  void OnManagerAdded(org::chromium::bluetooth::ManagerProxyInterface* adapter);
  void OnManagerRemoved(const dbus::ObjectPath& adapter_path);
  void OnAdapterAdded(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter);
  void OnAdapterRemoved(const dbus::ObjectPath& adapter_path);

  friend class BluetoothCallbackService;
  friend class ManagerCallbackService;

  // Interfaces for CallbackService to send events.
  void OnAdapterPoweredChanged(int32_t hci_interface, bool powered);
  void OnAdapterDiscoveringChanged(const dbus::ObjectPath& adapter_path,
                                   bool discovering);
  void OnDeviceAdded(const brillo::VariantDictionary& device);
  void OnDeviceRemoved(const brillo::VariantDictionary& device);

 private:
  void HandleRegisterBluetoothCallbackResponse(
      const dbus::ObjectPath& adapter_path,
      const dbus::ObjectPath& callback_path,
      brillo::Error* error,
      uint32_t register_id);

  void HandleRegisterManagerCallbackResponse(
      const dbus::ObjectPath& callback_path, brillo::Error* error);

  // Observer callback list.
  base::RepeatingCallbackList<void(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter)>
      adapter_added_observers_;
  base::RepeatingCallbackList<void(const dbus::ObjectPath& adapter_path)>
      adapter_removed_observers_;
  base::RepeatingCallbackList<void(int32_t, bool)>
      adapter_powered_changed_observers_;
  base::RepeatingCallbackList<void(const dbus::ObjectPath&, bool)>
      adapter_discovering_changed_observers_;
  base::RepeatingCallbackList<void(const brillo::VariantDictionary& device)>
      device_added_observers_;
  base::RepeatingCallbackList<void(const brillo::VariantDictionary& device)>
      device_removed_observers_;
  base::RepeatingCallbackList<void(const dbus::ObjectPath& adapter_path)>
      manager_removed_observers_;

  // Used to create Floss callback services.
  scoped_refptr<dbus::Bus> bus_;

  // Callback services.
  std::unique_ptr<ManagerCallbackService> manager_callback_;
  std::map<dbus::ObjectPath, std::unique_ptr<BluetoothCallbackService>>
      adapter_callbacks_;

  // The next index used to create callback service.
  uint32_t callback_path_index_ = 0;

  // Must be the last class member.
  base::WeakPtrFactory<FlossEventHub> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_
