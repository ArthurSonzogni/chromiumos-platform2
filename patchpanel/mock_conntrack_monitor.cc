// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_conntrack_monitor.h"

#include <chromeos/net-base/mock_socket.h>

#include "patchpanel/conntrack_monitor.h"

namespace patchpanel {
namespace {
constexpr ConntrackMonitor::EventType kConntrackEvents[] = {
    ConntrackMonitor::EventType::kNew,
    ConntrackMonitor::EventType::kUpdate,
    ConntrackMonitor::EventType::kDestroy,
};
}  // namespace

MockConntrackMonitor::MockConntrackMonitor()
    : ConntrackMonitor(kConntrackEvents,
                       std::make_unique<net_base::MockSocketFactory>()) {
  ON_CALL(*this, AddListener)
      .WillByDefault([&, this](base::span<const EventType> events,
                               const ConntrackEventHandler& callback) {
        return this->ConntrackMonitor::AddListener(events, callback);
      });
}

MockConntrackMonitor::~MockConntrackMonitor() = default;

}  // namespace patchpanel
