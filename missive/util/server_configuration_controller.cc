// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/server_configuration_controller.h"

#include <base/check.h>
#include "base/logging.h"
#include <base/functional/bind.h>
#include <base/memory/scoped_refptr.h>
#include <base/sequence_checker.h>
#include <base/strings/strcat.h>
#include <base/task/thread_pool.h>
#include <base/thread_annotations.h>

#include "missive/analytics/metrics.h"

namespace reporting {

ServerConfigurationController::BlockedDestinations::BlockedDestinations() {
  // When creating the object populate all the destinations to be non-blocked.
  ClearDestinations();
}

void ServerConfigurationController::BlockedDestinations::ClearDestinations() {
  for (int destination = 0; destination < Destination_ARRAYSIZE;
       ++destination) {
    blocked_destinations_[destination].store(false);
  }
}

bool ServerConfigurationController::BlockedDestinations::get(
    Destination destination) const {
  CHECK_LT(destination, Destination_ARRAYSIZE);
  return blocked_destinations_[destination].load();
}

void ServerConfigurationController::BlockedDestinations::blocked(
    Destination destination, bool blocked) {
  CHECK_LT(destination, Destination_ARRAYSIZE);
  const bool was_blocked = blocked_destinations_[destination].exchange(blocked);
  LOG_IF(WARNING, was_blocked != blocked)
      << "Destination " << Destination_Name(destination) << " switched to "
      << (blocked ? "blocked" : "unblocked");
}

// static
scoped_refptr<ServerConfigurationController>
ServerConfigurationController::Create(bool is_enabled_) {
  return base::WrapRefCounted(new ServerConfigurationController(is_enabled_));
}

ServerConfigurationController::ServerConfigurationController(bool is_enabled)
    : DynamicFlag("blocking_destinations_enabled", is_enabled) {}

ServerConfigurationController::~ServerConfigurationController() = default;

void ServerConfigurationController::UpdateConfiguration(
    ListOfBlockedDestinations destinations, HealthModule::Recorder recorder) {
  // Clear the destination list. The browser code already checks if the lists
  // are equal so there is no need to do that here.
  blocked_destinations_.ClearDestinations();

  // Update the list stored locally and emit a new health module record if
  // enabled.
  for (const auto destination : destinations.destinations()) {
    CHECK(Destination_IsValid(destination));
    auto current_destination = Destination(destination);
    if (recorder) {
      auto* const blocked_destination_list_record =
          recorder->mutable_blocked_destinations_updated_call();
      blocked_destination_list_record->add_destinations(current_destination);
    }
    blocked_destinations_.blocked(current_destination, true);
  }
}

bool ServerConfigurationController::IsDestinationBlocked(
    Destination destination) {
  // If the flag is not enabled then we don't block any records.
  if (!is_enabled()) {
    return false;
  }

  // Check if the destination has been blocked by the configuration file. If it
  // hasn't we just return false, if it has been blocked then we update the UMA
  // counter and generate a health module record if enabled.
  if (!blocked_destinations_.get(destination)) {
    return false;
  }

  // If the destination is blocked we want to log that we blocked a record
  // to our UMA metrics.
  analytics::Metrics::SendEnumToUMA(
      /*name=*/kConfigFileRecordBlocked, destination, Destination_ARRAYSIZE);

  return true;
}

}  // namespace reporting
