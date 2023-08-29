// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/mock_power_supply.h"

#include <base/check.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::Return;

namespace power_manager::system {

MockPowerSupply::MockPowerSupply() {
  ON_CALL(*this, AddObserver).WillByDefault([this](PowerSupplyObserver* o) {
    CHECK(o);
    observers_.AddObserver(o);
  });
  ON_CALL(*this, RemoveObserver).WillByDefault([this](PowerSupplyObserver* o) {
    CHECK(o);
    observers_.RemoveObserver(o);
  });
  ON_CALL(*this, GetPowerStatus).WillByDefault([this]() { return status_; });
  ON_CALL(*this, RefreshImmediately).WillByDefault([this]() {
    return refresh_result_;
  });
  ON_CALL(*this, SetSuspended).WillByDefault([this](bool s) {
    suspended_ = s;
  });
}

void MockPowerSupply::NotifyObservers() {
  for (PowerSupplyObserver& observer : observers_)
    observer.OnPowerStatusUpdate();
}

}  // namespace power_manager::system
