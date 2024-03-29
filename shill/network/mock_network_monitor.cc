// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_network_monitor.h"

#include "shill/network/network_monitor.h"
#include "shill/technology.h"

namespace shill {

MockNetworkMonitor::MockNetworkMonitor()
    : NetworkMonitor(nullptr,
                     nullptr,
                     nullptr,
                     nullptr,
                     Technology::kUnknown,
                     1,
                     "",
                     {},
                     NetworkMonitor::ValidationMode::kDisabled,
                     nullptr) {}

MockNetworkMonitor::~MockNetworkMonitor() = default;

MockNetworkMonitorFactory::MockNetworkMonitorFactory() = default;

MockNetworkMonitorFactory::~MockNetworkMonitorFactory() = default;

}  // namespace shill
