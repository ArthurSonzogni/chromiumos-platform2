// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_POWER_SUPPLY_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_POWER_SUPPLY_STUB_H_

#include "power_manager/powerd/system/power_supply.h"

#include <string>

namespace power_manager {
namespace system {

// Stub implementation of PowerSupplyInterface used by tests.
class PowerSupplyStub : public PowerSupplyInterface {
 public:
  PowerSupplyStub();
  virtual ~PowerSupplyStub();

  void set_refresh_result(bool result) { refresh_result_ = result; }
  void set_status(const PowerStatus& status) { status_ = status; }

  // PowerSupplyInterface implementation:
  void AddObserver(PowerSupplyObserver* observer) override;
  void RemoveObserver(PowerSupplyObserver* observer) override;
  PowerStatus GetPowerStatus() const override;
  bool RefreshImmediately() override;
  void SetSuspended(bool suspended) override;
  bool SetPowerSource(const std::string& id) override;

 private:
  // Result to return from RefreshImmediately().
  bool refresh_result_;

  // Status to return.
  PowerStatus status_;

  DISALLOW_COPY_AND_ASSIGN(PowerSupplyStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_POWER_SUPPLY_STUB_H_
