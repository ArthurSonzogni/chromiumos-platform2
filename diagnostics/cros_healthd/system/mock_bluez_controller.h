// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_BLUEZ_CONTROLLER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_BLUEZ_CONTROLLER_H_

#include <vector>

#include "diagnostics/cros_healthd/system/bluez_controller.h"

namespace diagnostics {

class MockBluezController final : public BluezController {
 public:
  MockBluezController() = default;
  MockBluezController(const MockBluezController&) = delete;
  MockBluezController& operator=(const MockBluezController&) = delete;
  ~MockBluezController() = default;

  MOCK_METHOD(std::vector<org::bluez::Adapter1ProxyInterface*>,
              GetAdapters,
              (),
              (const, override));
  MOCK_METHOD(std::vector<org::bluez::Device1ProxyInterface*>,
              GetDevices,
              (),
              (const, override));
  MOCK_METHOD(std::vector<org::bluez::AdminPolicyStatus1ProxyInterface*>,
              GetAdminPolicies,
              (),
              (const, override));
  MOCK_METHOD(std::vector<org::bluez::Battery1ProxyInterface*>,
              GetBatteries,
              (),
              (const, override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_BLUEZ_CONTROLLER_H_
