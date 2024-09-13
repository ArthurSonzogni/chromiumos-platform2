// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_TOUCHPAD_OBSERVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_TOUCHPAD_OBSERVER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics::test {

class MockTouchpadObserver
    : public ::ash::cros_healthd::mojom::TouchpadObserver {
 public:
  // ::ash::cros_healthd::mojom::TouchpadObserver overrides:
  MOCK_METHOD(void,
              OnConnected,
              (::ash::cros_healthd::mojom::TouchpadConnectedEventPtr),
              (override));
  MOCK_METHOD(void,
              OnTouch,
              (::ash::cros_healthd::mojom::TouchpadTouchEventPtr),
              (override));
  MOCK_METHOD(void,
              OnButton,
              (::ash::cros_healthd::mojom::TouchpadButtonEventPtr),
              (override));
};

}  // namespace diagnostics::test

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_TOUCHPAD_OBSERVER_H_
