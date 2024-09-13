// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_STYLUS_GARAGE_OBSERVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_STYLUS_GARAGE_OBSERVER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics::test {

class MockStylusGarageObserver
    : public ::ash::cros_healthd::mojom::StylusGarageObserver {
 public:
  // ::ash::cros_healthd::mojom::StylusGarageObserver overrides:
  MOCK_METHOD(void, OnInsert, (), (override));
  MOCK_METHOD(void, OnRemove, (), (override));
};

}  // namespace diagnostics::test

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TEST_MOCK_STYLUS_GARAGE_OBSERVER_H_
