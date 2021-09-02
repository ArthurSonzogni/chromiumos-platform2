// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_EVENT_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_EVENT_SUBSCRIBER_H_

#include <memory>

#include "diagnostics/cros_health_tool/event/audio_subscriber.h"
#include "diagnostics/cros_health_tool/event/bluetooth_subscriber.h"
#include "diagnostics/cros_health_tool/event/lid_subscriber.h"
#include "diagnostics/cros_health_tool/event/network_subscriber.h"
#include "diagnostics/cros_health_tool/event/power_subscriber.h"
#include "diagnostics/cros_health_tool/event/thunderbolt_subscriber.h"
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

  // Subscribes to cros_healthd's Bluetooth events. Returns true on success and
  // false on failure.
  bool SubscribeToBluetoothEvents();

  // Subscribes to cros_healthd's lid events. Returns true on success and false
  // on failure.
  bool SubscribeToLidEvents();

  // Subscribes to cros_healthd's network events. Returns true on success and
  // false on failure.
  bool SubscribeToNetworkEvents();

  // Subscribes to cros_healthd's power events. Returns true on success and
  // false on failure.
  bool SubscribeToPowerEvents();

  // Subscribes to cros_healthd's audio events. Returns true on success and
  // false on failure.
  bool SubscribeToAudioEvents();

  bool SubscribeToThunderboltEvents();

 private:
  // Allows mojo communication with cros_healthd.
  std::unique_ptr<CrosHealthdMojoAdapter> mojo_adapter_;

  // Used to subscribe to Bluetooth events.
  std::unique_ptr<BluetoothSubscriber> bluetooth_subscriber_;
  // Used to subscribe to lid events.
  std::unique_ptr<LidSubscriber> lid_subscriber_;
  // Used to subscribe to network events.
  std::unique_ptr<NetworkSubscriber> network_subscriber_;
  // Used to subscribe to power events.
  std::unique_ptr<PowerSubscriber> power_subscriber_;
  // Used to subscribe to audio events.
  std::unique_ptr<AudioSubscriber> audio_subscriber_;
  // Used to subscribe to thunderbolt events.
  std::unique_ptr<ThunderboltSubscriber> thunderbolt_subscriber_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_EVENT_SUBSCRIBER_H_
