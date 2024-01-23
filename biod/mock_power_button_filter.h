// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_POWER_BUTTON_FILTER_H_
#define BIOD_MOCK_POWER_BUTTON_FILTER_H_

#include "biod/power_button_filter_interface.h"

#include <gmock/gmock.h>

namespace biod {

class MockPowerButtonFilter : public PowerButtonFilterInterface {
 public:
  MockPowerButtonFilter() = default;
  ~MockPowerButtonFilter() override = default;

  MOCK_METHOD(bool, ShouldFilterFingerprintMatch, (), (override));
};

}  // namespace biod

#endif  // BIOD_MOCK_POWER_BUTTON_FILTER_H_
