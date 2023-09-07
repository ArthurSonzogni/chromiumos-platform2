// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_

#include <cstdint>
#include <string>
#include <gmock/gmock.h>
#include <optional>
#include <vector>

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {

// Mock implementation of the Executor interface.
class MockExecutor final : public ash::cros_healthd::mojom::Executor {
 public:
  MockExecutor() = default;
  MockExecutor(const MockExecutor&) = delete;
  MockExecutor& operator=(const MockExecutor&) = delete;
  ~MockExecutor() override = default;

  // ash::cros_healthd::mojom::Executor overrides:
  MOCK_METHOD(void, ReadFile, (File, ReadFileCallback), (override));
  MOCK_METHOD(void,
              ReadFilePart,
              (File, uint64_t, std::optional<uint64_t>, ReadFilePartCallback),
              (override));
  MOCK_METHOD(void, GetFileInfo, (File, GetFileInfoCallback), (override));
  MOCK_METHOD(void, GetAllFanSpeed, (GetAllFanSpeedCallback), (override));
  MOCK_METHOD(void,
              RunIw,
              (IwCommand, const std::string&, RunIwCallback),
              (override));
  MOCK_METHOD(void,
              RunMemtester,
              (uint32_t test_mem_kib,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   receiver),
              (override));
  MOCK_METHOD(void,
              GetProcessIOContents,
              (const std::vector<uint32_t>& pids, GetProcessIOContentsCallback),
              (override));
  MOCK_METHOD(void,
              ReadMsr,
              (uint32_t msr_reg, uint32_t cpu_index, ReadMsrCallback),
              (override));
  MOCK_METHOD(void, GetLidAngle, (GetLidAngleCallback), (override));
  MOCK_METHOD(void,
              GetFingerprintFrame,
              (ash::cros_healthd::mojom::FingerprintCaptureType type,
               GetFingerprintFrameCallback),
              (override));
  MOCK_METHOD(void,
              GetFingerprintInfo,
              (GetFingerprintInfoCallback),
              (override));
  MOCK_METHOD(void,
              SetLedColor,
              (ash::cros_healthd::mojom::LedName name,
               ash::cros_healthd::mojom::LedColor color,
               SetLedColorCallback callback),
              (override));
  MOCK_METHOD(void,
              ResetLedColor,
              (ash::cros_healthd::mojom::LedName name,
               ResetLedColorCallback callback),
              (override));
  MOCK_METHOD(void,
              GetHciDeviceConfig,
              (int32_t hci_interface, GetHciDeviceConfigCallback callback),
              (override));
  MOCK_METHOD(void,
              MonitorAudioJack,
              (mojo::PendingRemote<ash::cros_healthd::mojom::AudioJackObserver>
                   observer,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   process_control),
              (override));
  MOCK_METHOD(
      void,
      MonitorTouchpad,
      (mojo::PendingRemote<ash::cros_healthd::mojom::TouchpadObserver> observer,
       mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
           process_control),
      (override));
  MOCK_METHOD(void,
              FetchBootPerformance,
              (FetchBootPerformanceCallback),
              (override));
  MOCK_METHOD(void,
              MonitorTouchscreen,
              (mojo::PendingRemote<
                   ash::cros_healthd::mojom::TouchscreenObserver> observer,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   process_control),
              (override));
  MOCK_METHOD(void,
              MonitorStylusGarage,
              (mojo::PendingRemote<
                   ash::cros_healthd::mojom::StylusGarageObserver> observer,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   process_control),
              (override));
  MOCK_METHOD(
      void,
      MonitorStylus,
      (mojo::PendingRemote<ash::cros_healthd::mojom::StylusObserver> observer,
       mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
           process_control),
      (override));
  MOCK_METHOD(void, GetPsr, (GetPsrCallback), (override));
  MOCK_METHOD(void,
              FetchCrashFromCrashSender,
              (FetchCrashFromCrashSenderCallback),
              (override));
  MOCK_METHOD(void,
              RunStressAppTest,
              (uint32_t test_mem_mib,
               uint32_t test_seconds,
               ash::cros_healthd::mojom::StressAppTestType test_type,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   receiver),
              (override));
  MOCK_METHOD(void,
              RunFio,
              (ash::cros_healthd::mojom::FioJobArgumentPtr,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>),
              (override));
  MOCK_METHOD(void, RemoveFioTestFile, (RemoveFioTestFileCallback), (override));
  MOCK_METHOD(void,
              GetConnectedExternalDisplayConnectors,
              (GetConnectedExternalDisplayConnectorsCallback),
              (override));
  MOCK_METHOD(void,
              GetPrivacyScreenInfo,
              (GetPrivacyScreenInfoCallback),
              (override));
  MOCK_METHOD(void, FetchDisplayInfo, (FetchDisplayInfoCallback), (override));
  MOCK_METHOD(void,
              MonitorPowerButton,
              (mojo::PendingRemote<
                   ash::cros_healthd::mojom::PowerButtonObserver> observer,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   process_control),
              (override));
  MOCK_METHOD(void,
              RunPrimeSearch,
              (base::TimeDelta exec_duration,
               uint64_t max_num,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   process_control_receiver,
               RunPrimeSearchCallback callback),
              (override));
  MOCK_METHOD(void,
              MonitorVolumeButton,
              (mojo::PendingRemote<
                   ash::cros_healthd::mojom::VolumeButtonObserver> observer,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   process_control),
              (override));
  MOCK_METHOD(void,
              RunFloatingPoint,
              (base::TimeDelta exec_duration,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   process_control_receiver,
               RunFloatingPointCallback callback),
              (override));
  MOCK_METHOD(void,
              StartBtmon,
              (int32_t hci_interface,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   receiver),
              (override));
  MOCK_METHOD(void, ReadBtmonLog, (ReadBtmonLogCallback callback), (override));
  MOCK_METHOD(void, RemoveBtmonLog, (RemoveBtmonLogCallback), (override));
  MOCK_METHOD(void,
              SetFanSpeed,
              ((const base::flat_map<uint8_t, uint16_t>&)fan_rpms,
               SetFanSpeedCallback callback),
              (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_
