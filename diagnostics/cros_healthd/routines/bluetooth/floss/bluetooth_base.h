// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_BASE_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_BASE_H_

#include <optional>
#include <string>
#include <vector>

#include <base/callback_list.h>
#include <base/cancelable_callback.h>
#include <base/functional/callback_forward.h>
#include <base/memory/weak_ptr.h>
#include <base/time/tick_clock.h>
#include <brillo/errors/error.h>
#include <dbus/object_path.h>

#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"

namespace diagnostics {
class Context;
namespace floss {

// This class abstracts common interfaces for all Bluetooth related routines.
class BluetoothRoutineBase {
 public:
  explicit BluetoothRoutineBase(Context* context);
  BluetoothRoutineBase(const BluetoothRoutineBase&) = delete;
  BluetoothRoutineBase& operator=(const BluetoothRoutineBase&) = delete;
  ~BluetoothRoutineBase();

  // Used for reporting powered state when receiving powered changed events.
  // Reports null for unexpected error.
  using ResultCallback = base::OnceCallback<void(std::optional<bool>)>;

  // This function should be run when the routine starts. The callback will
  // return a bool state to report whether the initialization is successful.
  void Initialize(base::OnceCallback<void(bool)> on_finish);

  // Getter of the default Bluetooth adapter.
  org::chromium::bluetooth::BluetoothProxyInterface* GetDefaultAdapter() const;

  // Getter of the adapter initial powered state.
  bool GetAdapterInitialPoweredState() const;

  // Run pre-check for the Bluetooth routine. Bluetooth routine should not be
  // run when the adapter is already in discovery mode. The callback will return
  // null when pre-check is passed, otherwise a error string.
  void RunPreCheck(
      base::OnceCallback<void(std::optional<std::string>)> on_finish);

  // Set adapter powered state to `powered`. The callback will return the
  // powered state in D-Bus level after finishing this function. The callback
  // will return null if there is any error or the adapter powered changed event
  // is not received.
  void SetAdapterPoweredState(bool powered, ResultCallback on_finish);

  // Set `adapter_stop_discovery_` callback to stop discovery at the end of the
  // routine.
  void SetupStopDiscoveryJob();

 protected:
  // Unowned pointer that should outlive this instance.
  Context* const context_;
  // The HCI interface number of default adapter, will be set in `Initialize`.
  int32_t default_adapter_hci_ = -1;
  // The callback will be unregistered when the subscription is destructured.
  std::vector<base::CallbackListSubscription> event_subscriptions_;
  // Routine start time, used to calculate the progress percentage and timeout.
  base::TimeTicks start_ticks_;
  // A callback that should be run regardless of the execution status. This
  // callback will reset the adapter powered to `initial_powered_state_`.
  base::ScopedClosureRunner reset_bluetooth_powered_;
  // A callback that should be run regardless of the execution status. This
  // callback will ask the adapter to stop discovery.
  base::ScopedClosureRunner adapter_stop_discovery_;

 private:
  // Inner functions of `Initialize`.
  void CheckFlossEnabledState(base::OnceCallback<void(bool)> on_finish,
                              brillo::Error* error,
                              bool floss_enabled);
  void SetupDefaultAdapter(base::OnceCallback<void(bool)> on_finish,
                           brillo::Error* error,
                           int32_t hci_interface);
  void CheckAdapterEnabledState(base::OnceCallback<void(bool)> on_finish,
                                brillo::Error* error,
                                bool powered);

  // Inner functions of `RunPreCheck`.
  void HandleDiscoveringResponse(
      base::OnceCallback<void(std::optional<std::string>)> on_finish,
      brillo::Error* error,
      bool discovering);

  // Inner functions of `SetAdapterPoweredState`.
  void HandleSetPoweredResponse(bool powered,
                                ResultCallback on_finish,
                                brillo::Error* error);

  // Invoked when timeout of waiting for the adapter enabled event.
  void OnAdapterEnabledEventTimeout();

  void OnAdapterAdded(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter);
  void OnAdapterRemoved(const dbus::ObjectPath& adapter_path);
  void OnAdapterPoweredChanged(int32_t hci_interface, bool powered);
  void OnManagerRemoved(const dbus::ObjectPath& manager_path);

  // The initial powered state of the adapter.
  std::optional<bool> initial_powered_state_;

  // Current powered state, which will be initialized in `Initialize` and be
  // updated when `OnAdapterPoweredChanged` is invoked.
  bool current_powered_;

  // The Bluetooth manager from Floss.
  org::chromium::bluetooth::ManagerProxyInterface* manager_;

  // The default adapter from Floss, which is null when adapter is not enabled.
  // It will be initialized in `Initialize` and be updated when `OnAdapterAdded`
  // or `OnAdapterRemoved` is invoked.
  org::chromium::bluetooth::BluetoothProxyInterface* default_adapter_ = nullptr;

  // The callback waiting for adapter powered changed event and return the
  // powered state. The callback will return null if the routine doesn't receive
  // event before timeout.
  base::OnceCallback<void(std::optional<bool>)> on_adapter_powered_changed_cb_;

  // The timeout callback of adapter powered changed event.
  base::CancelableOnceClosure timeout_cb_;

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothRoutineBase> weak_ptr_factory_{this};
};

}  // namespace floss
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_BASE_H_
