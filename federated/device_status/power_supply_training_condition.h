// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_DEVICE_STATUS_POWER_SUPPLY_TRAINING_CONDITION_H_
#define FEDERATED_DEVICE_STATUS_POWER_SUPPLY_TRAINING_CONDITION_H_

#include <atomic>

#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>

#include "federated/device_status/training_condition.h"

namespace dbus {
class ObjectProxy;
class Signal;
class Bus;
}  // namespace dbus

namespace federated {

// Monitors the power supply status and answers whether there the conditions
// are satisfied. Currently, we check that the battery level is above 90% or
// the device is not discharging.
class PowerSupplyTrainingCondition : public TrainingCondition {
 public:
  explicit PowerSupplyTrainingCondition(dbus::Bus* bus);
  PowerSupplyTrainingCondition(const PowerSupplyTrainingCondition&) = delete;
  PowerSupplyTrainingCondition& operator=(const PowerSupplyTrainingCondition&) =
      delete;
  ~PowerSupplyTrainingCondition() override = default;

  // TrainingCondition:
  [[nodiscard]] bool IsTrainingConditionSatisfiedToStart() const override;
  [[nodiscard]] bool IsTrainingConditionSatisfiedToContinue() const override;

 private:
  // Processes powerd dbus signals/responses.
  void OnPowerManagerServiceAvailable(bool service_available);
  void OnPowerSupplyReceived(dbus::Signal* signal);
  void OnBatterySaverModeReceived(dbus::Signal* signal);

  // Obtained from dbus, should never delete it.
  dbus::ObjectProxy* const powerd_dbus_proxy_;

  // Whether the device has enough battery to start / continue jobs.
  // Updated in `OnPowerSupplyReceived`.
  bool enough_battery_to_start_;
  // This is thread-safe.
  std::atomic_bool enough_battery_to_continue_;

  // If `battery_saver_enabled_`, do not run the tasks.
  // Initialized in `OnPowerManagerServiceAvailable` and updated in
  // `OnBatterySaverModeReceived`.
  // This is thread-safe.
  std::atomic_bool battery_saver_enabled_;

  const base::WeakPtrFactory<PowerSupplyTrainingCondition> weak_ptr_factory_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace federated

#endif  // FEDERATED_DEVICE_STATUS_POWER_SUPPLY_TRAINING_CONDITION_H_
