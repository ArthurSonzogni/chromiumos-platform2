// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_CONNTRACK_MONITOR_H_
#define PATCHPANEL_MOCK_CONNTRACK_MONITOR_H_

#include <memory>

#include <base/containers/span.h>
#include <gmock/gmock.h>

#include "patchpanel/conntrack_monitor.h"

namespace patchpanel {

class MockConntrackMonitor : public ConntrackMonitor {
 public:
  MockConntrackMonitor();
  MockConntrackMonitor(const MockConntrackMonitor&) = delete;
  MockConntrackMonitor& operator=(const MockConntrackMonitor&) = delete;

  ~MockConntrackMonitor() override;

  void DispatchEventForTesting(const Event& msg) { DispatchEvent(msg); }
  MOCK_METHOD(void, Start, (base::span<const EventType>), (override));
  MOCK_METHOD(std::unique_ptr<Listener>,
              AddListener,
              (base::span<const EventType>, const ConntrackEventHandler&),
              (override));
};

}  // namespace patchpanel
#endif  // PATCHPANEL_MOCK_CONNTRACK_MONITOR_H_
