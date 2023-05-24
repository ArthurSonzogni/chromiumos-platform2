// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_

#include <vector>

#include <base/time/time.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/mojom/delegate.mojom.h"

namespace ec {
class EcCommandFactoryInterface;
}  // namespace ec

namespace diagnostics {

class DelegateImpl : public ash::cros_healthd::mojom::Delegate {
 public:
  explicit DelegateImpl(ec::EcCommandFactoryInterface* ec_command_factory);
  DelegateImpl(const DelegateImpl&) = delete;
  DelegateImpl& operator=(const DelegateImpl&) = delete;
  ~DelegateImpl() override;

  // ash::cros_healthd::mojom::Delegate overrides.
  void GetFingerprintFrame(
      ash::cros_healthd::mojom::FingerprintCaptureType type,
      GetFingerprintFrameCallback callback) override;
  void GetFingerprintInfo(GetFingerprintInfoCallback callback) override;
  void SetLedColor(ash::cros_healthd::mojom::LedName name,
                   ash::cros_healthd::mojom::LedColor color,
                   SetLedColorCallback callback) override;
  void ResetLedColor(ash::cros_healthd::mojom::LedName name,
                     ResetLedColorCallback callback) override;
  void MonitorAudioJack(
      mojo::PendingRemote<ash::cros_healthd::mojom::AudioJackObserver> observer)
      override;
  void MonitorTouchpad(
      mojo::PendingRemote<ash::cros_healthd::mojom::TouchpadObserver> observer)
      override;
  void FetchBootPerformance(FetchBootPerformanceCallback callback) override;
  void MonitorTouchscreen(
      mojo::PendingRemote<ash::cros_healthd::mojom::TouchscreenObserver>
          observer) override;
  void MonitorStylusGarage(
      mojo::PendingRemote<ash::cros_healthd::mojom::StylusGarageObserver>
          observer) override;
  void MonitorStylus(
      mojo::PendingRemote<ash::cros_healthd::mojom::StylusObserver> observer)
      override;
  void GetLidAngle(GetLidAngleCallback callback) override;
  void GetPsr(GetPsrCallback callback) override;
  void GetConnectedExternalDisplayConnectors(
      const std::optional<std::vector<uint32_t>>& last_known_connectors,
      GetConnectedExternalDisplayConnectorsCallback callback) override;
  void GetPrivacyScreenInfo(GetPrivacyScreenInfoCallback callback) override;
  void FetchDisplayInfo(FetchDisplayInfoCallback callback) override;
  void MonitorPowerButton(
      mojo::PendingRemote<ash::cros_healthd::mojom::PowerButtonObserver>
          observer) override;
  void RunPrimeSearch(base::TimeDelta exec_duration,
                      uint64_t max_num,
                      RunPrimeSearchCallback callback) override;
  void MonitorVolumeButton(
      mojo::PendingRemote<ash::cros_healthd::mojom::VolumeButtonObserver>
          observer) override;
  void RunFloatingPoint(base::TimeDelta exec_duration,
                        RunFloatingPointCallback callback) override;
  void GetAllFanSpeed(GetAllFanSpeedCallback callback) override;
  void SetFanSpeed(const base::flat_map<uint8_t, uint16_t>& fan_id_to_rpm,
                   SetFanSpeedCallback callback) override;
  void SetAllFanAutoControl(SetAllFanAutoControlCallback callback) override;
  void GetEcThermalSensors(GetEcThermalSensorsCallback callback) override;
  void GetTouchpadDevices(GetTouchpadDevicesCallback callback) override;

 private:
  void GetConnectedExternalDisplayConnectorsHelper(
      std::optional<std::vector<uint32_t>> last_known_connectors,
      GetConnectedExternalDisplayConnectorsCallback callback,
      int times);

  ec::EcCommandFactoryInterface* const ec_command_factory_;

  // Must be the last class member.
  base::WeakPtrFactory<DelegateImpl> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_
