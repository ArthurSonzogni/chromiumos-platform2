// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_DEVICE_STATUS_MEMORY_PRESSURE_TRAINING_CONDITION_H_
#define FEDERATED_DEVICE_STATUS_MEMORY_PRESSURE_TRAINING_CONDITION_H_

#include <atomic>
#include <map>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>

#include "federated/device_status/training_condition.h"

namespace dbus {
class ObjectProxy;
class Signal;
class Bus;
}  // namespace dbus

namespace federated {

// Monitors the memory pressure level and answers whether the conditions
// are satisfied.
class MemoryPressureTrainingCondition : public TrainingCondition {
 public:
  explicit MemoryPressureTrainingCondition(dbus::Bus* bus);
  MemoryPressureTrainingCondition(const MemoryPressureTrainingCondition&) =
      delete;
  MemoryPressureTrainingCondition& operator=(
      const MemoryPressureTrainingCondition&) = delete;
  ~MemoryPressureTrainingCondition() override = default;

  // TrainingCondition:
  [[nodiscard]] bool IsTrainingConditionSatisfiedToStart() const override;
  [[nodiscard]] bool IsTrainingConditionSatisfiedToContinue() const override;

 private:
  // Processes memory level dbus signals.
  void OnMemoryPressureSignalReceived(const std::string& signal_name,
                                      dbus::Signal* signal);

  // Obtained from dbus, should never delete it.
  dbus::ObjectProxy* const resource_dbus_proxy_ = nullptr;

  // Stores the received memory levels.
  std::map<std::string, uint32_t> memory_levels_;

  bool satisfactory_to_start_ = true;
  // This is thread-safe.
  std::atomic_bool satisfactory_to_continue_ = true;

  base::WeakPtrFactory<MemoryPressureTrainingCondition> weak_ptr_factory_{this};

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace federated

#endif  // FEDERATED_DEVICE_STATUS_MEMORY_PRESSURE_TRAINING_CONDITION_H_
