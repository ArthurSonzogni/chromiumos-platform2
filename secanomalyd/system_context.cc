// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/system_context.h"

#include <ios>  // std::boolalpha
#include <map>
#include <string>

SystemContext::SystemContext(SessionManagerProxyInterface* session_manager)
    : session_manager_{session_manager} {
  Refresh();
}

void SystemContext::Refresh() {
  std::ignore = UpdateLoggedInState();
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
