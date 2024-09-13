// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_POWER_BUTTON_OBSERVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_POWER_BUTTON_OBSERVER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics::test {

class MockPowerButtonObserver
    : public ::ash::cros_healthd::mojom::PowerButtonObserver {
 public:
  // ::ash::cros_healthd::mojom::PowerButtonObserver overrides:
  MOCK_METHOD(void,
              OnEvent,
              (::ash::cros_healthd::mojom::PowerButtonObserver::ButtonState),
              (override));
  MOCK_METHOD(void, OnConnectedToEventNode, (), (override));
};

}  // namespace diagnostics::test

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_POWER_BUTTON_OBSERVER_H_
