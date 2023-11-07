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

namespace federated {
namespace {

// Resource manager defines several Enums to represent the memory level.
// For signal MemoryPressureChrome, the items are 0=None, 1=Moderate,
// 2=Critical. For MemoryPressureArcvm,  they are 0=None, 1=Cached,
// 2=Perceptible, 3=Foreground. We treat 0 and 1 as acceptable. See
// system_api/dbus/resource_manager/dbus-constants.h.
// TODO(b:306077663): use fine-grained levels for different signals.
const uint8_t kMaxAcceptableLevel = 1;

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

bool MemoryPressureTrainingCondition::IsTrainingConditionSatisfied() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Requires all signals are satisfied.
  // `std::all_of` returns true if the container is empty (i.e. when no signals
  // received yet), which is an intended behavior.
  return std::all_of(memory_levels_.begin(), memory_levels_.end(),
                     [](auto const& iter) { return iter.second; });
}

void MemoryPressureTrainingCondition::OnMemoryPressureSignalReceived(
    const std::string& signal_name, dbus::Signal* const signal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  memory_levels_[signal_name] = false;
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
  // emitted together, but if arc vm level is None, only chrome level signal is
  // emitted. That means if arc vm level becomes non 0, it never becomes 0
  // again. The following logic is required to fix the should-have "None"
  // signals.
  if (signal_name == resource_manager::kMemoryPressureChrome &&
      pressure_level == 0) {
    memory_levels_.clear();
    return;
  }

  memory_levels_[signal_name] = pressure_level <= kMaxAcceptableLevel;

  DVLOG(1) << "Set memory_levels_[" << signal_name
           << "] = " << memory_levels_[signal_name]
           << " because its pressure_level="
           << static_cast<uint32_t>(pressure_level);
}

}  // namespace federated
