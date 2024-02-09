// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_MOCK_POWER_MANAGER_H_
#define UPDATE_ENGINE_CROS_MOCK_POWER_MANAGER_H_

#include <gmock/gmock.h>

#include "update_engine/cros/power_manager_interface.h"

namespace chromeos_update_engine {

class MockPowerManager : public PowerManagerInterface {
 public:
  MockPowerManager() = default;

  MOCK_METHOD0(RequestReboot, bool(void));
  MOCK_METHOD0(RequestShutdown, bool(void));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_MOCK_POWER_MANAGER_H_
