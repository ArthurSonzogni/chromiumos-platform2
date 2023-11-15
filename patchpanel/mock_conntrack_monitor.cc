// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_conntrack_monitor.h"

namespace patchpanel {

MockConntrackMonitor::MockConntrackMonitor() {
  SetEventMaskForTesting(kNewEventBitMask | kDestroyEventBitMask |
                         kUpdateEventBitMask);
  ON_CALL(*this, AddListener)
      .WillByDefault([&, this](base::span<const EventType> events,
                               const ConntrackEventHandler& callback) {
        return this->ConntrackMonitor::AddListener(events, callback);
      });
}

MockConntrackMonitor::~MockConntrackMonitor() = default;

}  // namespace patchpanel
