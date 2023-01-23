// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/system_context.h"

#include <ios>  // std::boolalpha
#include <map>
#include <string>

#include "secanomalyd/mount_entry.h"

SystemContext::SystemContext(SessionManagerProxyInterface* session_manager)
    : session_manager_{session_manager} {
  std::ignore = UpdateLoggedInState();
}

void SystemContext::Refresh() {
  std::ignore = UpdateLoggedInState();
  UpdateKnownMountsState();
}

bool SystemContext::UpdateLoggedInState() {
  brillo::ErrorPtr error;
  std::map<std::string, std::string> sessions;
  session_manager_->RetrieveActiveSessions(&sessions, &error);

  if (error) {
    LOG(ERROR) << "Error making D-Bus proxy call to interface "
               << "'" << session_manager_->GetObjectPath().value()
               << "': " << error->GetMessage();
    logged_in_ = false;
    return false;
  }
  logged_in_ = sessions.size() > 0;
  VLOG(1) << "logged_in_ -> " << std::boolalpha << logged_in_;
  return true;
}

void SystemContext::UpdateKnownMountsState() {
  previous_known_mounts_.clear();
  previous_known_mounts_.merge(current_known_mounts_);
}

bool SystemContext::IsMountPersistent(const base::FilePath& known_mount) const {
  return (previous_known_mounts_.count(known_mount) == 1);
}

void SystemContext::RecordKnownMountObservation(
    const base::FilePath& known_mount) {
  // Ensures `known_mount` is indeed a predefined known mount.
  if (secanomalyd::kKnownMounts.count(known_mount) == 0) {
    return;
  }
  current_known_mounts_.insert(known_mount);
}
