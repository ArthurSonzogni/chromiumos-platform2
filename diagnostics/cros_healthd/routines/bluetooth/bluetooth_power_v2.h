// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_POWER_V2_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_POWER_V2_H_

#include <string>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom-forward.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_base_v2.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {

// This routine is supported when ChromeOS is using Floss instead of Bluez.
//
// The Bluetooth power routine checks that the Bluetooth adapter's power
// functionality is working correctly by checking the off and on powered status
// in D-Bus level and in HCI level.
class BluetoothPowerRoutineV2 final : public BaseRoutineControl,
                                      public BluetoothRoutineBaseV2 {
 public:
  explicit BluetoothPowerRoutineV2(
      Context* context,
      const ash::cros_healthd::mojom::BluetoothPowerRoutineArgumentPtr& arg);
  BluetoothPowerRoutineV2(const BluetoothPowerRoutineV2&) = delete;
  BluetoothPowerRoutineV2& operator=(const BluetoothPowerRoutineV2&) = delete;
  ~BluetoothPowerRoutineV2() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  void RunNextStep();

  // Handle the response of routine initialization.
  void HandleInitializeResult(bool success);

  // Handle the response of pre-check.
  void HandlePreCheckResponse(std::optional<std::string> error);

  // Handle the response of changing powered state.
  void HandleChangePoweredResponse(
      const base::expected<bool, std::string>& result);

  // Observe adapter powered changed events to check the adapter powered in
  // D-Bus level.
  void OnAdapterPoweredChanged(int32_t hci_interface, bool powered);

  // Handle the response of hciconfig to check the adapter powered in HCI level.
  void HandleHciConfigResponse(
      bool dbus_powered,
      ash::cros_healthd::mojom::ExecutedProcessResultPtr result);

  // Check the powered property of adapter in D-Bus and HCI level.
  void ValidateAdapterPowered(bool dbus_powered, bool hci_powered);

  // Update the routine percentage.
  void UpdatePercentage();

  // Routine timeout function.
  void OnTimeoutOccurred();

  // Set the routine result and stop other callbacks.
  void SetResultAndStop(const base::expected<bool, std::string>& result);

  enum class TestStep : int32_t {
    kInitialize = 0,
    kPreCheckDiscovery = 1,
    kCheckPoweredStatusOff = 2,
    kCheckPoweredStatusOn = 3,
    kComplete = 4,  // Should be the last one. New step should be added before
                    // it.
  };
  TestStep step_ = TestStep::kInitialize;

  // Detail of routine output.
  ash::cros_healthd::mojom::BluetoothPowerRoutineDetailPtr routine_output_;

  // Cancelable task to update the routine percentage.
  base::CancelableOnceClosure percentage_update_task_;

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothPowerRoutineV2> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_POWER_V2_H_
