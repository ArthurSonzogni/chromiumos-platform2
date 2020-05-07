// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_EVENT_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_EVENT_SUBSCRIBER_H_

#include <memory>

#include "diagnostics/cros_healthd_event_tool/bluetooth_subscriber.h"
#include "diagnostics/cros_healthd_event_tool/power_subscriber.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"

namespace diagnostics {

// This class connects all category-specific event subscribers to cros_healthd.
class EventSubscriber final {
 public:
  // Creates an instance, initially not subscribed to any events.
  EventSubscriber();
  EventSubscriber(const EventSubscriber&) = delete;
  EventSubscriber& operator=(const EventSubscriber&) = delete;
  ~EventSubscriber();

  // Subscribes to cros_healthd's Bluetooth events.
  void SubscribeToBluetoothEvents();

  // Subscribes to cros_healthd's power events.
  void SubscribeToPowerEvents();

 private:
  // Allows mojo communication with cros_healthd.
  CrosHealthdMojoAdapter mojo_adapter_;

  // Used to subscribe to Bluetooth events.
  std::unique_ptr<BluetoothSubscriber> bluetooth_subscriber_;
  // Used to subscribe to power events.
  std::unique_ptr<PowerSubscriber> power_subscriber_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENT_TOOL_EVENT_SUBSCRIBER_H_
