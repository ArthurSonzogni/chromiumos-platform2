// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/event_management.h"

#include <string>

#include <absl/container/flat_hash_set.h>
#include <trunks/dbus_transceiver.h>

#include "libhwsec/status.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

EventManagementTpm2::~EventManagementTpm2() {
  for (const std::string& event : events) {
    if (Status status = Stop(event); !status.ok()) {
      LOG(WARNING) << "Failed to stop event(" << event << "): " << status;
    }
  }
}

StatusOr<ScopedEvent> EventManagementTpm2::Start(const std::string& event) {
  trunks::DbusTransceiver* trunks_dbus = context_.GetDbusTransceiver();
  if (!trunks_dbus) {
    return MakeStatus<TPMError>("No trunks D-Bus interface",
                                TPMRetryAction::kNoRetry);
  }

  if (events.contains(event)) {
    return MakeStatus<TPMError>("Event already exists",
                                TPMRetryAction::kNoRetry);
  }
  events.insert(event);
  trunks_dbus->StartEvent(event);

  return ScopedEvent(event, middleware_derivative_);
}

Status EventManagementTpm2::Stop(const std::string& event) {
  trunks::DbusTransceiver* trunks_dbus = context_.GetDbusTransceiver();
  if (!trunks_dbus) {
    return MakeStatus<TPMError>("No trunks D-Bus interface",
                                TPMRetryAction::kNoRetry);
  }

  if (events.erase(event) != 1) {
    return MakeStatus<TPMError>("Event not found", TPMRetryAction::kNoRetry);
  }
  trunks_dbus->StopEvent(event);

  return OkStatus();
}

}  // namespace hwsec
