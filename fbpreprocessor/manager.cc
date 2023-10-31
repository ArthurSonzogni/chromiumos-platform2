// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/manager.h"

#include <memory>

#include <dbus/bus.h>

#include "fbpreprocessor/configuration.h"
#include "fbpreprocessor/input_manager.h"
#include "fbpreprocessor/output_manager.h"
#include "fbpreprocessor/platform_features_client.h"
#include "fbpreprocessor/pseudonymization_manager.h"
#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

Manager::Manager(const Configuration& config)
    : default_file_expiration_in_secs_(config.default_expiration_secs()) {}

void Manager::Start(dbus::Bus* bus) {
  // SessionStateManager must be instantiated first since the other modules will
  // register as observers with the SessionStateManager::Observer interface.
  // Same thing for PlatformFeaturesClient, other modules will also register as
  // observers so it must be instantiated before the observers.
  session_state_manager_ = std::make_unique<SessionStateManager>(bus);
  platform_features_ = std::make_unique<PlatformFeaturesClient>();

  pseudonymization_manager_ = std::make_unique<PseudonymizationManager>(this);
  output_manager_ = std::make_unique<OutputManager>(this);
  input_manager_ = std::make_unique<InputManager>(this, bus);

  platform_features_->Start(bus);
  // Now that the daemon is fully initialized, notify everyone if a user was
  // logged in when the daemon started.
  session_state_manager_->RefreshPrimaryUser();
}

Manager::~Manager() = default;

bool Manager::FirmwareDumpsAllowed() {
  if (session_state_manager_.get() == nullptr) {
    LOG(ERROR) << "SessionStateManager not instantiated.";
    return false;
  }
  if (platform_features_.get() == nullptr) {
    LOG(ERROR) << "PlatformFeaturesClient not instantiated.";
    return false;
  }
  return session_state_manager_->FirmwareDumpsAllowedByPolicy() &&
         platform_features_->FirmwareDumpsAllowedByFinch();
}

}  // namespace fbpreprocessor
