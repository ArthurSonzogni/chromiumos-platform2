// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/manager_impl.h"

#include <memory>

#include <base/task/sequenced_task_runner.h>
#include <dbus/bus.h>

#include "fbpreprocessor/configuration.h"
#include "fbpreprocessor/crash_reporter_dbus_adaptor.h"
#include "fbpreprocessor/input_manager.h"
#include "fbpreprocessor/output_manager.h"
#include "fbpreprocessor/platform_features_client.h"
#include "fbpreprocessor/pseudonymization_manager.h"
#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

ManagerImpl::ManagerImpl(const Configuration& config)
    : default_file_expiration_in_secs_(config.default_expiration_secs()) {}

ManagerImpl::~ManagerImpl() = default;

void ManagerImpl::Start(dbus::Bus* bus) {
  CHECK(base::SequencedTaskRunner::HasCurrentDefault())
      << "No default task runner.";
  task_runner_ = base::SequencedTaskRunner::GetCurrentDefault();

  // |SessionStateManager| and |PlatformFeaturesClient| must be instantiated
  // before other objects that will register as their |Observer|.
  platform_features_ = std::make_unique<PlatformFeaturesClient>();
  // |SessionStateManager| is an |Observer| of |PlatformFeaturesClient| so we
  // instantiate it after.
  session_state_manager_ = std::make_unique<SessionStateManager>(this, bus);

  pseudonymization_manager_ = std::make_unique<PseudonymizationManager>(this);
  output_manager_ = std::make_unique<OutputManager>(this);
  input_manager_ = std::make_unique<InputManager>(this);

  crash_reporter_dbus_adaptor_ =
      std::make_unique<CrashReporterDBusAdaptor>(this, bus);

  platform_features_->Start(bus);
  // Now that the daemon is fully initialized, notify everyone if a user was
  // logged in when the daemon started.
  session_state_manager_->RefreshPrimaryUser();
}

SessionStateManagerInterface* ManagerImpl::session_state_manager() const {
  return session_state_manager_.get();
}

bool ManagerImpl::FirmwareDumpsAllowed() const {
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
