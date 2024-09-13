// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_VOLUME_BUTTON_OBSERVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_VOLUME_BUTTON_OBSERVER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics::test {

class MockVolumeButtonObserver
    : public ::ash::cros_healthd::mojom::VolumeButtonObserver {
 public:
  // ::ash::cros_healthd::mojom::VolumeButtonObserver overrides:
  MOCK_METHOD(void,
              OnEvent,
              (::ash::cros_healthd::mojom::VolumeButtonObserver::Button,
               ::ash::cros_healthd::mojom::VolumeButtonObserver::ButtonState),
              (override));
};

}  // namespace diagnostics::test

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_VOLUME_BUTTON_OBSERVER_H_
