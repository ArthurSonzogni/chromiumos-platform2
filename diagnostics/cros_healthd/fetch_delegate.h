// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCH_DELEGATE_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCH_DELEGATE_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {

// The FetchDelegate interface performs work on behalf of FetchAggregator.
class FetchDelegate {
 public:
  virtual ~FetchDelegate() = default;

  // Fetches audio info and passes the result to the callback.
  virtual void FetchAudioResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::AudioResultPtr)>
          callback) = 0;

  // Fetches audio hardware info and passes the result to the callback.
  virtual void FetchAudioHardwareResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::AudioHardwareResultPtr)>
          callback) = 0;

  // Returns the device's backlight info.
  virtual ash::cros_healthd::mojom::BacklightResultPtr
  FetchBacklightResult() = 0;

  // Fetches battery info and passes the result to the callback.
  virtual void FetchBatteryResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::BatteryResultPtr)>
          callback) = 0;

  // Fetches boot performance info and passes the result to the callback.
  virtual void FetchBootPerformanceResult(
      base::OnceCallback<void(
          ash::cros_healthd::mojom::BootPerformanceResultPtr)> callback) = 0;

  // Fetches Bluetooth info and passes the result to the callback.
  virtual void FetchBluetoothResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::BluetoothResultPtr)>
          callback) = 0;

  // Fetches bus info and passes the result to the callback.
  virtual void FetchBusResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::BusResultPtr)>
          callback) = 0;

  // Fetches cpu info and passes the result to the callback.
  virtual void FetchCpuResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::CpuResultPtr)>
          callback) = 0;

  // Fetches display info and passes the result to the callback.
  virtual void FetchDisplayResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::DisplayResultPtr)>
          callback) = 0;

  // Fetches fan info and passes the result to the callback.
  virtual void FetchFanResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::FanResultPtr)>
          callback) = 0;

  // Fetches graphics info and passes the result to the callback.
  virtual void FetchGraphicsResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::GraphicsResultPtr)>
          callback) = 0;

  // Fetches input info and passes the result to the callback.
  virtual void FetchInputResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::InputResultPtr)>
          callback) = 0;

  // Fetches memory info and passes the result to the callback.
  virtual void FetchMemoryResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::MemoryResultPtr)>
          callback) = 0;

  // Fetches network info and passes the result to the callback.
  virtual void FetchNetworkResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::NetworkResultPtr)>
          callback) = 0;

  // Fetches network interface info and passes the result to the callback.
  virtual void FetchNetworkInterfaceResult(
      base::OnceCallback<void(
          ash::cros_healthd::mojom::NetworkInterfaceResultPtr)> callback) = 0;

  // Returns the device's non-removable block device info.
  virtual ash::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr
  FetchNonRemovableBlockDevicesResult() = 0;

  // Fetches sensor info and passes the result to the callback.
  virtual void FetchSensorResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::SensorResultPtr)>
          callback) = 0;

  // Fetches stateful partition info and passes the result to the callback.
  virtual void FetchStatefulPartitionResult(
      base::OnceCallback<void(
          ash::cros_healthd::mojom::StatefulPartitionResultPtr)> callback) = 0;

  // Fetches system info and passes the result to the callback.
  virtual void FetchSystemResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::SystemResultPtr)>
          callback) = 0;

  // Fetches thermal info and passes the result to the callback.
  virtual void FetchThermalResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::ThermalResultPtr)>
          callback) = 0;

  // Returns the device's timezone info.
  virtual ash::cros_healthd::mojom::TimezoneResultPtr FetchTimezoneResult() = 0;

  // Fetches tpm info and passes the result to the callback.
  virtual void FetchTpmResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::TpmResultPtr)>
          callback) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCH_DELEGATE_H_
