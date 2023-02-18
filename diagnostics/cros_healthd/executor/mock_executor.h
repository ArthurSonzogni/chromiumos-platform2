// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_

#include <string>
#include <gmock/gmock.h>
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
  MOCK_METHOD(void, GetFanSpeed, (GetFanSpeedCallback), (override));
  MOCK_METHOD(void, GetInterfaces, (GetInterfacesCallback), (override));
  MOCK_METHOD(void,
              GetLink,
              (const std::string& interface_name, GetLinkCallback),
              (override));
  MOCK_METHOD(void,
              GetInfo,
              (const std::string& interface_name, GetInfoCallback),
              (override));
  MOCK_METHOD(void,
              GetScanDump,
              (const std::string& interface_name, GetScanDumpCallback),
              (override));
  MOCK_METHOD(void,
              RunMemtester,
              (uint32_t test_mem_kib, RunMemtesterCallback),
              (override));
  MOCK_METHOD(void,
              RunMemtesterV2,
              (uint32_t test_mem_kib,
               mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                   receiver),
              (override));
  MOCK_METHOD(void, KillMemtester, (), (override));
  MOCK_METHOD(void,
              GetProcessIOContents,
              (const std::vector<uint32_t>& pids, GetProcessIOContentsCallback),
              (override));
  MOCK_METHOD(void,
              ReadMsr,
              (uint32_t msr_reg, uint32_t cpu_index, ReadMsrCallback),
              (override));
  MOCK_METHOD(void,
              GetUEFISecureBootContent,
              (GetUEFISecureBootContentCallback),
              (override));
  MOCK_METHOD(void,
              GetUEFIPlatformSizeContent,
              (GetUEFIPlatformSizeContentCallback),
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
              (GetHciDeviceConfigCallback),
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
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_
