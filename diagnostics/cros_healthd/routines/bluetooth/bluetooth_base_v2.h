// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_BASE_V2_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_BASE_V2_H_

#include <optional>
#include <string>
#include <vector>

#include <base/callback_list.h>
#include <base/functional/callback_forward.h>
#include <base/time/tick_clock.h>
#include <base/types/expected.h>
#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>
#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"

namespace diagnostics {

// This class abstracts common interfaces for all Bluetooth related routines.
class BluetoothRoutineBaseV2 {
 public:
  explicit BluetoothRoutineBaseV2(Context* context);
  BluetoothRoutineBaseV2(const BluetoothRoutineBaseV2&) = delete;
  BluetoothRoutineBaseV2& operator=(const BluetoothRoutineBaseV2&) = delete;
  ~BluetoothRoutineBaseV2();

  using ResultCallback =
      base::OnceCallback<void(const base::expected<bool, std::string>&)>;

  // This function should be run when the routine starts. The callback will
  // return a bool state to report whether the initialization is successful.
  void Initialize(base::OnceCallback<void(bool)> on_finish);

  // Getter of the default Bluetooth adapter.
  org::chromium::bluetooth::BluetoothProxyInterface* GetDefaultAdapter() const;

  // Getter of the adapter initial powered state.
  bool GetAdapterInitialPoweredState() const;

  // Run pre-check for the Bluetooth routine. Bluetooth routine should not be
  // run when the adapter is already in discovery mode. The callback will return
  // a bool state to report whether the pre-check is passed on completion, or a
  // string when unexpected error occurs.
  void RunPreCheck(ResultCallback on_finish);

  // Change adapter powered state to |powered|. The callback will return a bool
  // state to report whether the changing is succeeded on completion, or a
  // string when unexpected error occurs.
  void ChangeAdapterPoweredState(bool powered, ResultCallback on_finish);

 protected:
  // Unowned pointer that should outlive this instance.
  Context* const context_;
  // The Bluetooth manager from Floss.
  org::chromium::bluetooth::ManagerProxyInterface* manager_;
  // The HCI interface number of default adapter, will be set in |Initialize|.
  int32_t default_adapter_hci_ = -1;
  // The default adapter from Floss, which is null when adapter is not enabled.
  org::chromium::bluetooth::BluetoothProxyInterface* default_adapter_ = nullptr;
  // The callback will be unregistered when the subscription is destructured.
  std::vector<base::CallbackListSubscription> event_subscriptions_;
  // Routine start time, used to calculate the progress percentage and timeout.
  base::TimeTicks start_ticks_;
  // A callback that should be run regardless of the execution status. This
  // callback will reset the adapter powered to |initial_powered_state_|.
  base::ScopedClosureRunner reset_bluetooth_powered_;

 private:
  // Inner functions of |Initialize|.
  void SetupDefaultAdapter(base::OnceCallback<void(bool)> on_finish,
                           brillo::Error* error,
                           int32_t hci_interface);
  void CheckAdapterEnabledState(base::OnceCallback<void(bool)> on_finish,
                                brillo::Error* error,
                                bool powered);

  // Inner functions of |RunPreCheck|.
  void CheckAdapterDiscoveringState(ResultCallback on_finish,
                                    brillo::Error* error,
                                    bool powered);
  void HandleDiscoveringResponse(ResultCallback on_finish,
                                 brillo::Error* error,
                                 bool discovering);

  // Inner functions of |ChangeAdapterPoweredState|.
  void HandleChangePoweredResponse(bool powered,
                                   ResultCallback on_finish,
                                   brillo::Error* error);

  void OnAdapterAdded(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter);
  void OnAdapterRemoved(const dbus::ObjectPath& adapter_path);
  void OnManagerRemoved(const dbus::ObjectPath& manager_path);

  // The initial powered state of the adapter.
  std::optional<bool> initial_powered_state_;

  // The callbacks waiting for adapter added event.
  std::vector<base::OnceClosure> on_adapter_added_cbs_;

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothRoutineBaseV2> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_BASE_V2_H_
