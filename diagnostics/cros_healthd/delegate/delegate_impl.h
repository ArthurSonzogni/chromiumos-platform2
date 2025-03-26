// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <chromeos/ec/ec_commands.h>
#include <libec/ec_command_version_supported.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/mojom/delegate.mojom.h"

namespace base {
class TimeDelta;
}  // namespace base

namespace ec {
class EcCommandFactoryInterface;
class MkbpEvent;

class EcCommandVersionSupported : public EcCommandVersionSupportedInterface {
 public:
  EcCommandVersionSupported() = default;
  EcCommandVersionSupported(const EcCommandVersionSupported&) = delete;
  EcCommandVersionSupported& operator=(const EcCommandVersionSupported&) =
      delete;

  virtual ~EcCommandVersionSupported() = default;

  EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                  uint32_t ver) override;
};

}  // namespace ec

namespace diagnostics {
class DisplayUtilFactory;
class CpuRoutineTaskDelegate;
class EvdevMonitor;
class EvdevMonitorDelegate;

class DelegateImpl : public ash::cros_healthd::mojom::Delegate {
 public:
  // The maximum number of times we will retry getting external display info.
  static constexpr int kMaximumGetExternalDisplayInfoRetry = 10;

  // The interval to wait between retrying to get external display info.
  static constexpr base::TimeDelta kGetExternalDisplayInfoRetryPeriod =
      base::Milliseconds(500);

  explicit DelegateImpl(ec::EcCommandFactoryInterface* ec_command_factory,
                        DisplayUtilFactory* display_util_factory);
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
  void GetSmartBatteryManufactureDate(
      uint8_t i2c_port,
      GetSmartBatteryManufactureDateCallback callback) override;
  void GetSmartBatteryTemperature(
      uint8_t i2c_port, GetSmartBatteryTemperatureCallback callback) override;
  void RunUrandom(base::TimeDelta exec_duration,
                  RunUrandomCallback callback) override;
  void RunNetworkBandwidthTest(
      ash::cros_healthd::mojom::NetworkBandwidthTestType type,
      const std::string& oem_name,
      mojo::PendingRemote<ash::cros_healthd::mojom::NetworkBandwidthObserver>
          observer,
      RunNetworkBandwidthTestCallback callback) override;
  void FetchGraphicsInfo(FetchGraphicsInfoCallback callback) override;

 protected:
  // Creates an unowned `EvdevMonitor`. The caller is responsible for
  // maintaining its lifetime.
  [[nodiscard]] virtual EvdevMonitor* CreateEvdevMonitor(
      std::unique_ptr<EvdevMonitorDelegate> delegate);

  // Mark as virtual to be overridden in tests.
  virtual std::unique_ptr<ec::MkbpEvent> CreateMkbpEvent(
      int fd, enum ec_mkbp_event event_type);

  // Mark as virtual to be overridden in tests.
  virtual std::unique_ptr<CpuRoutineTaskDelegate>
  CreatePrimeNumberSearchDelegate(uint64_t max_num);

  // Mark as virtual to be overridden in tests.
  virtual std::unique_ptr<CpuRoutineTaskDelegate> CreateFloatingPointDelegate();

  // Mark as virtual to be overridden in tests.
  virtual std::unique_ptr<CpuRoutineTaskDelegate> CreateUrandomDelegate();

 private:
  // Starts monitor evdev events with the given `EvdevMonitorDelegate`. See
  // `EvdevMonitor::StartMonitoring()` for the meaning of
  // `allow_multiple_devices`.
  void MonitorEvdevEvents(std::unique_ptr<EvdevMonitorDelegate> delegate,
                          bool allow_multiple_devices);

  ec::EcCommandFactoryInterface* const ec_command_factory_;
  DisplayUtilFactory* const display_util_factory_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_
