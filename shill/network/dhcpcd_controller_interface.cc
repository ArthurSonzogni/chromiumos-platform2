// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcpcd_controller_interface.h"

#include <string_view>

#include <base/functional/callback.h>

namespace shill {

DHCPCDControllerInterface::DHCPCDControllerInterface(std::string_view interface,
                                                     EventHandler* handler)
    : interface_(interface), handler_(handler) {}

DHCPCDControllerInterface::~DHCPCDControllerInterface() = default;

void DHCPCDControllerInterface::OnProcessExited(int pid, int exit_status) {
  handler_->OnProcessExited(pid, exit_status);
}

}  // namespace shill
