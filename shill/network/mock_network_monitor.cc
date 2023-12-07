// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_network_monitor.h"

#include <base/functional/callback_helpers.h>

namespace shill {

MockNetworkMonitor::MockNetworkMonitor()
    : NetworkMonitor(nullptr, "", {}, base::DoNothing()) {}

MockNetworkMonitor::~MockNetworkMonitor() = default;

}  // namespace shill
