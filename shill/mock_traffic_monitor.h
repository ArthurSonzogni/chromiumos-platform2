// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_TRAFFIC_MONITOR_H_
#define SHILL_MOCK_TRAFFIC_MONITOR_H_

#include "shill/traffic_monitor.h"

#include <gmock/gmock.h>

namespace shill {

class MockTrafficMonitor : public TrafficMonitor {
 public:
  MockTrafficMonitor();
  virtual ~MockTrafficMonitor();

  MOCK_METHOD0(Start, void());
  MOCK_METHOD0(Stop, void());

  DISALLOW_COPY_AND_ASSIGN(MockTrafficMonitor);
};

}  // namespace shill

#endif  // SHILL_MOCK_TRAFFIC_MONITOR_H_
