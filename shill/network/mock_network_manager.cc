// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_network_manager.h"

namespace shill {

MockNetworkManager::MockNetworkManager()
    : NetworkManager(nullptr, nullptr, nullptr) {}

MockNetworkManager::~MockNetworkManager() = default;

}  // namespace shill
