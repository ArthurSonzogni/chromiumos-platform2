// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_

#include <base/callback_list.h>
#include <dbus/object_path.h>

#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"

namespace diagnostics {

using OnFlossAdapterAddedCallback = base::RepeatingCallback<void(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter)>;
using OnFlossAdapterRemovedCallback =
    base::RepeatingCallback<void(const dbus::ObjectPath& adapter_path)>;

// Interface for subscribing Bluetooth events via Floss proxies.
class FlossEventHub {
 public:
  explicit FlossEventHub(
      org::chromium::bluetooth::ObjectManagerProxy* bluetooth_proxy = nullptr);
  FlossEventHub(const FlossEventHub&) = delete;
  FlossEventHub& operator=(const FlossEventHub&) = delete;
  ~FlossEventHub() = default;

  base::CallbackListSubscription SubscribeAdapterAdded(
      OnFlossAdapterAddedCallback callback);
  base::CallbackListSubscription SubscribeAdapterRemoved(
      OnFlossAdapterRemovedCallback callback);

  // TODO(b/300239296): Support adapter property changed and device added,
  // removed and property changed events.

 protected:
  // Interfaces for subclass to send events.
  void OnAdapterAdded(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter);
  void OnAdapterRemoved(const dbus::ObjectPath& adapter_path);

 private:
  // Observer callback list.
  base::RepeatingCallbackList<void(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter)>
      adapter_added_observers_;
  base::RepeatingCallbackList<void(const dbus::ObjectPath& adapter_path)>
      adapter_removed_observers_;

  // Must be the last class member.
  base::WeakPtrFactory<FlossEventHub> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_
