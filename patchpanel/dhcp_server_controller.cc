// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dhcp_server_controller.h"

#include <base/logging.h>

namespace patchpanel {

DHCPServerController::DHCPServerController(const std::string& ifname)
    : ifname_(ifname) {}

DHCPServerController::~DHCPServerController() {
  Stop();
}

bool DHCPServerController::Start() {
  LOG(INFO) << "Starting DHCP server at: " << ifname_;

  // TODO(b/271371399): Implement the method.
  return true;
}

void DHCPServerController::Stop() {
  LOG(INFO) << "Stopping DHCP server at: " << ifname_;

  // TODO(b/271371399): Implement the method.
}

}  // namespace patchpanel
