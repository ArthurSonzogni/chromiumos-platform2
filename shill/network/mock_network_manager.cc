// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_network_manager.h"

#include "shill/control_interface.h"

namespace shill {

MockNetworkManager::MockNetworkManager(ControlInterface* control_interface)
    : NetworkManager(control_interface, nullptr, nullptr) {}

MockNetworkManager::~MockNetworkManager() = default;

}  // namespace shill
