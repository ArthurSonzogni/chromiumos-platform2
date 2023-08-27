// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/manager.h"

#include <memory>

#include <dbus/bus.h>

#include "fbpreprocessor/output_manager.h"
#include "fbpreprocessor/pseudonymization_manager.h"
#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

Manager::Manager(dbus::Bus* bus) : bus_(bus) {
  // SessionStateManager must be instantiated first since the other modules will
  // register as observers with the SessionStateManager::Observer interface.
  session_state_manager_ = std::make_unique<SessionStateManager>(bus_.get());
  pseudonymization_manager_ = std::make_unique<PseudonymizationManager>(this);
  output_manager_ = std::make_unique<OutputManager>(this);

  // Now that the daemon is fully initialized, notify everyone if a user was
  // logged in when the daemon started.
  session_state_manager_->RefreshPrimaryUser();
}

Manager::~Manager() = default;

}  // namespace fbpreprocessor
