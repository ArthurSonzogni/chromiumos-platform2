// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_POWER_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_POWER_H_

#include <string>

#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom-forward.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_base.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {
class Context;
namespace floss {

// This routine is supported when ChromeOS is using Floss instead of Bluez.
//
// The Bluetooth power routine checks that the Bluetooth adapter's power
// functionality is working correctly by checking the off and on powered status
// in D-Bus level and in HCI level.
class BluetoothPowerRoutine final : public BaseRoutineControl,
                                    public BluetoothRoutineBase {
 public:
  explicit BluetoothPowerRoutine(
      Context* context,
      const ash::cros_healthd::mojom::BluetoothPowerRoutineArgumentPtr& arg);
  BluetoothPowerRoutine(const BluetoothPowerRoutine&) = delete;
  BluetoothPowerRoutine& operator=(const BluetoothPowerRoutine&) = delete;
  ~BluetoothPowerRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  void RunNextStep();

  // Handle the response of routine initialization.
  void HandleInitializeResult(bool success);

  // Handle the response of pre-check.
  void HandlePreCheckResponse(std::optional<std::string> error);

  // Handle the response of setting powered state.
  void HandleSetPoweredResponse(std::optional<bool> dbus_powered);

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

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothPowerRoutine> weak_ptr_factory_{this};
};

}  // namespace floss
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_POWER_H_
