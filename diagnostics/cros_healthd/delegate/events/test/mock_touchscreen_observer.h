// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_TOUCHSCREEN_OBSERVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_TOUCHSCREEN_OBSERVER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics::test {

class MockTouchscreenObserver
    : public ::ash::cros_healthd::mojom::TouchscreenObserver {
 public:
  // ::ash::cros_healthd::mojom::TouchscreenObserver overrides:
  MOCK_METHOD(void,
              OnConnected,
              (::ash::cros_healthd::mojom::TouchscreenConnectedEventPtr),
              (override));
  MOCK_METHOD(void,
              OnTouch,
              (::ash::cros_healthd::mojom::TouchscreenTouchEventPtr),
              (override));
};

}  // namespace diagnostics::test

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_TOUCHSCREEN_OBSERVER_H_
