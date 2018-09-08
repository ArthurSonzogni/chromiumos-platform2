// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_traffic_monitor.h"

#include "shill/device.h"

namespace shill {

MockTrafficMonitor::MockTrafficMonitor() : TrafficMonitor(nullptr, nullptr) {}

MockTrafficMonitor::~MockTrafficMonitor() {}

}  // namespace shill
