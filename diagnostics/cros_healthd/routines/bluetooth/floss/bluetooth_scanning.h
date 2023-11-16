// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_SCANNING_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_SCANNING_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <brillo/variant_dictionary.h>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_base.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom-forward.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {
class Context;

namespace floss {
enum class BtPropertyType : uint32_t;

// Frequency to poll the peripheral's RSSI info.
constexpr base::TimeDelta kScanningRoutineRssiPollingPeriod =
    base::Milliseconds(250);

// This routine is supported when ChromeOS is using Floss instead of Bluez.
//
// The Bluetooth scanning routine checks that the Bluetooth adapter can scan
// nearby Bluetooth peripherals and collect nearby peripherals' information.
class BluetoothScanningRoutine final : public BaseRoutineControl,
                                       public BluetoothRoutineBase {
 public:
  using CreateResult =
      base::expected<std::unique_ptr<BaseRoutineControl>,
                     ash::cros_healthd::mojom::SupportStatusPtr>;
  using CreateCallback = base::OnceCallback<void(CreateResult)>;

  static void Create(
      Context* context,
      const ash::cros_healthd::mojom::BluetoothScanningRoutineArgumentPtr& arg,
      CreateCallback callback);

  BluetoothScanningRoutine(const BluetoothScanningRoutine&) = delete;
  BluetoothScanningRoutine& operator=(const BluetoothScanningRoutine&) = delete;
  ~BluetoothScanningRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 protected:
  explicit BluetoothScanningRoutine(
      Context* context,
      const ash::cros_healthd::mojom::BluetoothScanningRoutineArgumentPtr& arg);

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

  // Handle the response of updating discovery mode.
  void HandleUpdateDiscoveryResponse(brillo::Error* error, bool is_success);

  // Observe device added and device property changed events to collect RSSI.
  void OnDeviceAdded(const brillo::VariantDictionary& device);
  void OnDevicePropertyChanged(const brillo::VariantDictionary& device,
                               BtPropertyType property);

  // Check and store scanned peripheral.
  void StoreScannedPeripheral(const brillo::VariantDictionary& device);

  // Get the scanned peripheral's RSSI.
  void GetPeripheralRssi(const brillo::VariantDictionary& device);

  // Handle the response of the peripheral RSSI.
  void HandleRssiResponse(const std::string& address,
                          brillo::Error* error,
                          int16_t rssi);

  // Update the routine percentage.
  void UpdatePercentage();

  // Scanning Routine completion function.
  void OnScanningFinished();

  // Routine timeout function.
  void OnTimeoutOccurred();

  // Set the routine result and stop other callbacks.
  void SetResultAndStop(const base::expected<bool, std::string>& result);

  enum class TestStep : int32_t {
    kInitialize = 0,
    kPreCheckDiscovery = 1,
    kEnsurePoweredOn = 2,
    kStartDiscovery = 3,
    kScanning = 4,
    kCancelDiscovery = 5,
    kComplete = 6,  // Should be the last one. New step should be added before
                    // it.
  };
  TestStep step_ = TestStep::kInitialize;

  // Routine arguments:
  // Expected duration to run the scanning routine.
  const base::TimeDelta exec_duration_;

  // Detail of routine output.
  struct ScannedPeripheral {
    std::vector<int16_t> rssi_history;
    std::optional<std::string> name;
  };

  // Scanned peripherals. The key is the peripheral's address.
  std::map<std::string, ScannedPeripheral> scanned_peripherals_;
  // RSSI polling callbacks for scanned peripherals. The key is the peripheral's
  // address.
  std::map<std::string, base::RepeatingClosure> polling_rssi_callbacks_;

  // Cancelable task to update the routine percentage.
  base::CancelableOnceClosure percentage_update_task_;

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothScanningRoutine> weak_ptr_factory_{this};
};

}  // namespace floss
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_SCANNING_H_
