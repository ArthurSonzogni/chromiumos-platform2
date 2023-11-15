// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <string>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "federated/memory_pressure_training_condition.h"
#include "federated/metrics.h"

namespace federated {
namespace {

// Resource manager defines several Enums to represent the memory level.
// For signal MemoryPressureChrome, the items are 0=None, 1=Moderate,
// 2=Critical.
// For MemoryPressureArcvm,  they are 0=None, 1=Cached, 2=Perceptible,
// 3=Foreground.
// See system_api/dbus/resource_manager/dbus-constants.h.

// Allow to start new jobs when Chrome memory pressure level is None.
const uint32_t kMaxAcceptableChromeLevelToStart = 0;
// Allow to continue existing jobs when Arc vm memory pressure level <=
// Cached.
const uint32_t kMaxAcceptableArcvmLevelToContinue = 1;
// This default value is greater than any possible levels.
const uint32_t kDefaultUnsatisfiedLevel = 100;

void OnSignalConnected(const std::string& interface_name,
                       const std::string& signal_name,
                       bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << interface_name << ":"
               << signal_name;
  }
}
}  // namespace

MemoryPressureTrainingCondition::MemoryPressureTrainingCondition(dbus::Bus* bus)
    : resource_dbus_proxy_(bus->GetObjectProxy(
          resource_manager::kResourceManagerServiceName,
          dbus::ObjectPath(resource_manager::kResourceManagerServicePath))) {
  DCHECK_NE(resource_dbus_proxy_, nullptr);

  // The memory level signals are broadcasted periodically.
  // TODO(b:306077663): remove this after we have
  // kMemoryPressureFederatedService.
  resource_dbus_proxy_->ConnectToSignal(
      resource_manager::kResourceManagerInterface,
      resource_manager::kMemoryPressureChrome,
      base::BindRepeating(
          &MemoryPressureTrainingCondition::OnMemoryPressureSignalReceived,
          weak_ptr_factory_.GetMutableWeakPtr(),
          resource_manager::kMemoryPressureChrome),
      base::BindOnce(&OnSignalConnected));

  resource_dbus_proxy_->ConnectToSignal(
      resource_manager::kResourceManagerInterface,
      resource_manager::kMemoryPressureArcvm,
      base::BindRepeating(
          &MemoryPressureTrainingCondition::OnMemoryPressureSignalReceived,
          weak_ptr_factory_.GetMutableWeakPtr(),
          resource_manager::kMemoryPressureArcvm),
      base::BindOnce(&OnSignalConnected));

  DVLOG(1) << "Construct MemoryPressureTrainingCondition";
}

bool MemoryPressureTrainingCondition::IsTrainingConditionSatisfiedToStart()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const auto iter =
      memory_levels_.find(resource_manager::kMemoryPressureChrome);
  // Non existing signal in `memory_levels_` means it's None.
  bool satisfied = iter == memory_levels_.end() ||
                   iter->second <= kMaxAcceptableChromeLevelToStart;

  if (!satisfied) {
    Metrics::GetInstance()->LogTrainingConditionToStartResult(
        TrainingConditionResult::kMemoryPressureHigh);
  }

  return satisfied;
}

bool MemoryPressureTrainingCondition::IsTrainingConditionSatisfiedToContinue()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const auto iter = memory_levels_.find(resource_manager::kMemoryPressureArcvm);
  // Non existing signal in `memory_levels_` means it's None.
  bool satisfied = iter == memory_levels_.end() ||
                   iter->second <= kMaxAcceptableArcvmLevelToContinue;
  if (!satisfied) {
    Metrics::GetInstance()->LogTrainingConditionToContinueResult(
        TrainingConditionResult::kMemoryPressureHigh);
  }

  return satisfied;
}

void MemoryPressureTrainingCondition::OnMemoryPressureSignalReceived(
    const std::string& signal_name, dbus::Signal* const signal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  memory_levels_[signal_name] = kDefaultUnsatisfiedLevel;

  if (signal == nullptr) {
    DVLOG(1) << "Received a null signal in OnMemoryPressureSignalReceived.";
    return;
  }

  dbus::MessageReader reader(signal);
  uint8_t pressure_level = 0;
  if (!reader.PopByte(&pressure_level)) {
    DVLOG(1) << "Failed to read pressure level from dbus message.";
    return;
  }

  // As per resourced src, the different memory level signals are usually
  // emitted together, but if arc vm level is None, only chrome level signal
  // is emitted. That means if arc vm level becomes non 0, it never becomes 0
  // again. The following logic is required to fix the should-have "None"
  // signals.
  if (signal_name == resource_manager::kMemoryPressureChrome &&
      pressure_level == 0) {
    memory_levels_.clear();
    return;
  }

  memory_levels_[signal_name] = static_cast<uint32_t>(pressure_level);

  DVLOG(1) << "Set memory_levels_[" << signal_name
           << "] = " << static_cast<uint32_t>(pressure_level);
}

}  // namespace federated
