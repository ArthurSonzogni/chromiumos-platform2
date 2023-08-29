// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_MOCK_POWER_SUPPLY_H_
#define POWER_MANAGER_POWERD_SYSTEM_MOCK_POWER_SUPPLY_H_

#include "power_manager/powerd/system/power_supply.h"

#include <string>

#include <base/observer_list.h>
#include <base/time/time.h>

#include <gmock/gmock.h>

namespace power_manager::system {

// Mock implementation of PowerSupplyInterface used by tests.
class MockPowerSupply : public PowerSupplyInterface {
 public:
  MockPowerSupply();

  MockPowerSupply(const MockPowerSupply&) = delete;
  MockPowerSupply& operator=(const MockPowerSupply&) = delete;

  ~MockPowerSupply() override = default;

  void set_refresh_result(bool result) { refresh_result_ = result; }
  void set_status(const PowerStatus& status) { status_ = status; }

  bool suspended() const { return suspended_; }

  // Notifies registered observers that the power status has been updated.
  void NotifyObservers();

  // PowerSupplyInterface implementation:
  MOCK_METHOD(void, AddObserver, (PowerSupplyObserver*), (override));
  MOCK_METHOD(void, RemoveObserver, (PowerSupplyObserver*), (override));
  MOCK_METHOD(PowerStatus, GetPowerStatus, (), (const, override));
  MOCK_METHOD(bool, RefreshImmediately, (), (override));
  MOCK_METHOD(void, SetSuspended, (bool), (override));
  MOCK_METHOD(void, SetAdaptiveChargingSupported, (bool), (override));
  MOCK_METHOD(void, SetAdaptiveChargingHeuristicEnabled, (bool), (override));
  MOCK_METHOD(void,
              SetAdaptiveCharging,
              (const base::TimeDelta&, double),
              (override));
  MOCK_METHOD(void, ClearAdaptiveChargingChargeDelay, (), (override));
  MOCK_METHOD(void, SetChargeLimited, (double), (override));
  MOCK_METHOD(void, ClearChargeLimited, (), (override));
  MOCK_METHOD(void, OnBatterySaverStateChanged, (), (override));

 private:
  // Result to return from RefreshImmediately().
  bool refresh_result_ = true;
  bool suspended_ = false;

  // Status to return.
  PowerStatus status_;

  base::ObserverList<PowerSupplyObserver> observers_;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_MOCK_POWER_SUPPLY_H_
