// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_DISCOVERY_V2_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_DISCOVERY_V2_H_

#include <optional>
#include <string>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/executor/utils/scoped_process_control.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_base_v2.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {

// This routine is supported when ChromeOS is using Floss instead of Bluez.
//
// The Bluetooth discovery routine checks that the Bluetooth adapter can start
// and stop discovery mode correctly by checking the on and off discovering
// status in D-Bus level and in HCI level.
class BluetoothDiscoveryRoutineV2 final : public BaseRoutineControl,
                                          public BluetoothRoutineBaseV2 {
 public:
  explicit BluetoothDiscoveryRoutineV2(
      Context* context,
      const ash::cros_healthd::mojom::BluetoothDiscoveryRoutineArgumentPtr&
          arg);
  BluetoothDiscoveryRoutineV2(const BluetoothDiscoveryRoutineV2&) = delete;
  BluetoothDiscoveryRoutineV2& operator=(const BluetoothDiscoveryRoutineV2&) =
      delete;
  ~BluetoothDiscoveryRoutineV2() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  void RunNextStep();

  // Handle the response of routine initialization.
  void HandleInitializeResult(bool success);

  // Handle the response of pre-check.
  void HandlePreCheckResponse(std::optional<std::string> error);

  // Handle the response of changing powered state.
  void HandleEnsurePoweredOnResponse(
      const base::expected<bool, std::string>& result);

  // Update the adapter to start or stop discovery mode.
  void UpdateAdapterDiscoveryMode();

  // Handle the error of updating discovery mode.
  void HandleUpdateDiscoveryError(brillo::Error* error);

  // Read the btmon logs.
  void ReadBtmonLog(int retry_count);

  // Ensure the btmon process is ready by reading log file.
  void EnsureBtmonReady(
      int retry_count,
      ash::cros_healthd::mojom::ExecutedProcessResultPtr result);

  // Check the discovering property of adapter in D-Bus level.
  void OnAdapterDiscoveringChanged(const dbus::ObjectPath& adapter_path,
                                   bool discovering);

  // Check the discovering status in HCI level by checking HCI traces.
  void CheckBtmonHciTraces(
      ash::cros_healthd::mojom::ExecutedProcessResultPtr result);

  // Check the discovering property of adapter in D-Bus and HCI level.
  void ValidateAdapterDiscovering(bool hci_discovering);

  // Update the routine percentage.
  void UpdatePercentage();

  // Routine timeout function.
  void OnTimeoutOccurred();

  // Set the routine result and stop other callbacks.
  void SetResultAndStop(const base::expected<bool, std::string>& result);

  enum class TestStep : int32_t {
    kInitialize = 0,
    kPreCheckDiscovery = 1,
    kEnsurePoweredOn = 2,
    kSetupBtmon = 3,
    kCheckDiscoveringStatusOn = 4,
    kCheckDiscoveringStatusOff = 5,
    kComplete = 6,  // Should be the last one. New step should be added before
                    // it.
  };
  TestStep step_ = TestStep::kInitialize;

  // Detail of routine output.
  ash::cros_healthd::mojom::BluetoothDiscoveryRoutineDetailPtr routine_output_;

  // Default adapter path, which will be set after initialized.
  dbus::ObjectPath default_adapter_path_;

  // Current discovering state in D-Bus level, which should be off at first.
  bool current_dbus_discovering_ = false;

  // A scoped version of process control that manages the lifetime of the btmon
  // process.
  ScopedProcessControl scoped_process_control_;

  // The line number of the btmon log last checked.
  uint32_t log_line_last_checked = 0;

  // Cancelable task to update the routine percentage.
  base::CancelableOnceClosure percentage_update_task_;

  // A callback that should be run regardless of the execution status. This
  // callback will remove temporary log file created by btmon.
  base::ScopedClosureRunner remove_btmon_log_;
  // A callback that should be run regardless of the execution status. This
  // callback will ask the adapter to stop discovery.
  base::ScopedClosureRunner adapter_stop_discovery_;

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothDiscoveryRoutineV2> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_DISCOVERY_V2_H_
