// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_FLOSS_CONTROLLER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_FLOSS_CONTROLLER_H_

#include <vector>

#include "diagnostics/cros_healthd/system/floss_controller.h"

namespace diagnostics {

class MockFlossController final : public FlossController {
 public:
  MockFlossController() = default;
  MockFlossController(const MockFlossController&) = delete;
  MockFlossController& operator=(const MockFlossController&) = delete;
  ~MockFlossController() = default;

  MOCK_METHOD(org::chromium::bluetooth::ManagerProxyInterface*,
              GetManager,
              (),
              (const, override));
  MOCK_METHOD(std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>,
              GetAdapters,
              (),
              (const, override));
  MOCK_METHOD(
      std::vector<org::chromium::bluetooth::BluetoothAdminProxyInterface*>,
      GetAdmins,
      (),
      (const, override));
  MOCK_METHOD(std::vector<org::chromium::bluetooth::BluetoothQAProxyInterface*>,
              GetAdapterQAs,
              (),
              (const, override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_FLOSS_CONTROLLER_H_
