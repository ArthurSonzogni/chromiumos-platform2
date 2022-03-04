// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_

#include <string>
#include <gmock/gmock.h>

#include "diagnostics/mojom/private/cros_healthd_executor.mojom.h"

namespace diagnostics {

// Mock implementation of the Executor interface.
class MockExecutor final
    : public chromeos::cros_healthd_executor::mojom::Executor {
 public:
  MockExecutor() = default;
  MockExecutor(const MockExecutor&) = delete;
  MockExecutor& operator=(const MockExecutor&) = delete;
  ~MockExecutor() override = default;

  // chromeos::cros_healthd_executor::mojom::Executor overrides:
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
  MOCK_METHOD(void, RunMemtester, (RunMemtesterCallback), (override));
  MOCK_METHOD(void, KillMemtester, (), (override));
  MOCK_METHOD(void,
              GetProcessIOContents,
              (uint32_t pid, GetProcessIOContentsCallback),
              (override));
  MOCK_METHOD(void, ReadMsr, (uint32_t msr_reg, ReadMsrCallback), (override));
  MOCK_METHOD(void,
              GetUEFISecureBootContent,
              (GetUEFISecureBootContentCallback),
              (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_MOCK_EXECUTOR_H_
