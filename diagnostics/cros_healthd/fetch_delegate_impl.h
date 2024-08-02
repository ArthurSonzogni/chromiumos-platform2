// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCH_DELEGATE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCH_DELEGATE_IMPL_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/cros_healthd/fetch_delegate.h"
#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class Context;

class FetchDelegateImpl : public FetchDelegate {
 public:
  explicit FetchDelegateImpl(Context* context);
  FetchDelegateImpl(const FetchDelegateImpl&) = delete;
  FetchDelegateImpl& operator=(const FetchDelegateImpl&) = delete;
  ~FetchDelegateImpl() override;

  // FetchDelegate overrides:
  void FetchAudioResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::AudioResultPtr)>
          callback) override;

  void FetchAudioHardwareResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::AudioHardwareResultPtr)>
          callback) override;

  ash::cros_healthd::mojom::BacklightResultPtr FetchBacklightResult() override;

  void FetchBatteryResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::BatteryResultPtr)>
          callback) override;

  void FetchBluetoothResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::BluetoothResultPtr)>
          callback) override;

  void FetchBootPerformanceResult(
      base::OnceCallback<
          void(ash::cros_healthd::mojom::BootPerformanceResultPtr)> callback)
      override;

  void FetchBusResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::BusResultPtr)> callback)
      override;

  void FetchCpuResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::CpuResultPtr)> callback)
      override;

  void FetchDisplayResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::DisplayResultPtr)>
          callback) override;

  void FetchFanResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::FanResultPtr)> callback)
      override;

  void FetchGraphicsResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::GraphicsResultPtr)>
          callback) override;

  void FetchInputResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::InputResultPtr)>
          callback) override;

  void FetchMemoryResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::MemoryResultPtr)>
          callback) override;

  void FetchNetworkResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::NetworkResultPtr)>
          callback) override;

  void FetchNetworkInterfaceResult(
      base::OnceCallback<
          void(ash::cros_healthd::mojom::NetworkInterfaceResultPtr)> callback)
      override;

  ash::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr
  FetchNonRemovableBlockDevicesResult() override;

  void FetchSensorResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::SensorResultPtr)>
          callback) override;

  void FetchStatefulPartitionResult(
      base::OnceCallback<
          void(ash::cros_healthd::mojom::StatefulPartitionResultPtr)> callback)
      override;

  void FetchSystemResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::SystemResultPtr)>
          callback) override;

  void FetchThermalResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::ThermalResultPtr)>
          callback) override;

  ash::cros_healthd::mojom::TimezoneResultPtr FetchTimezoneResult() override;

  void FetchTpmResult(
      base::OnceCallback<void(ash::cros_healthd::mojom::TpmResultPtr)> callback)
      override;

 private:
  DiskFetcher disk_fetcher_;

  // Unowned. The following instances should outlive this instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCH_DELEGATE_IMPL_H_
