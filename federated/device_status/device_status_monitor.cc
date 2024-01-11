// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <memory>
#include <utility>

#include "federated/device_status/device_status_monitor.h"
#include "federated/device_status/memory_pressure_training_condition.h"
#include "federated/device_status/network_status_training_condition.h"
#include "federated/device_status/power_supply_training_condition.h"
#include "federated/device_status/shill_proxy_impl.h"
#include "federated/metrics.h"

namespace federated {

DeviceStatusMonitor::DeviceStatusMonitor(
    std::vector<std::unique_ptr<TrainingCondition>> training_conditions)
    : training_conditions_(std::move(training_conditions)) {
  DVLOG(1) << "Creating DeviceStatusMonitor";
}

std::unique_ptr<DeviceStatusMonitor> DeviceStatusMonitor::CreateFromDBus(
    dbus::Bus* bus) {
  std::vector<std::unique_ptr<TrainingCondition>> training_conditions;
  training_conditions.push_back(
      std::make_unique<PowerSupplyTrainingCondition>(bus));

  training_conditions.push_back(
      std::make_unique<NetworkStatusTrainingCondition>(
          new ShillProxyImpl(bus)));

  training_conditions.push_back(
      std::make_unique<MemoryPressureTrainingCondition>(bus));

  return std::make_unique<DeviceStatusMonitor>(std::move(training_conditions));
}

bool DeviceStatusMonitor::TrainingConditionsSatisfiedToStart() const {
  DVLOG(1) << "DeviceStatusMonitor::TrainingConditionsSatisfiedToStart()";
  bool satisfied = true;
  for (const auto& condition : training_conditions_) {
    satisfied = satisfied && condition->IsTrainingConditionSatisfiedToStart();
  }

  Metrics::GetInstance()->LogTrainingConditionToStartResult(
      satisfied ? TrainingConditionResult::kPass
                : TrainingConditionResult::kFailed);

  return satisfied;
}

bool DeviceStatusMonitor::TrainingConditionsSatisfiedToContinue() const {
  DVLOG(1) << "DeviceStatusMonitor::TrainingConditionsSatisfiedToContinue()";
  bool satisfied = true;
  for (const auto& condition : training_conditions_) {
    satisfied =
        satisfied && condition->IsTrainingConditionSatisfiedToContinue();
  }

  Metrics::GetInstance()->LogTrainingConditionToContinueResult(
      satisfied ? TrainingConditionResult::kPass
                : TrainingConditionResult::kFailed);

  return satisfied;
}

}  // namespace federated
