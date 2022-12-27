// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_EVENT_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_EVENT_SUBSCRIBER_H_

#include <memory>

#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_health_tool/event/audio_subscriber.h"
#include "diagnostics/cros_health_tool/event/bluetooth_subscriber.h"
#include "diagnostics/cros_health_tool/event/lid_subscriber.h"
#include "diagnostics/cros_health_tool/event/network_subscriber.h"
#include "diagnostics/cros_health_tool/event/power_subscriber.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

// This class connects all category-specific event subscribers to cros_healthd.
class EventSubscriber final : public ash::cros_healthd::mojom::EventObserver {
 public:
  // Creates an instance, initially not subscribed to any events.
  EventSubscriber();
  EventSubscriber(const EventSubscriber&) = delete;
  EventSubscriber& operator=(const EventSubscriber&) = delete;
  ~EventSubscriber();

  // ash::cros_healthd::mojom::EventObserver overrides:
  void OnEvent(const ash::cros_healthd::mojom::EventInfoPtr info) override;

  // Subscribes to cros_healthd's Bluetooth events.
  void SubscribeToBluetoothEvents();

  // Subscribes to cros_healthd's lid events.
  void SubscribeToLidEvents();

  // Subscribes to cros_healthd's network events.
  void SubscribeToNetworkEvents();

  // Subscribes to cros_healthd's power events.
  void SubscribeToPowerEvents();

  // Subscribes to cros_healthd's audio events.
  void SubscribeToAudioEvents();

  // Subscribes to cros_healthd's events.
  void SubscribeToEvents(ash::cros_healthd::mojom::EventCategoryEnum category);

 private:
  // Allows mojo communication with cros_healthd event service.
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdEventService>
      event_service_;

  mojo::Receiver<ash::cros_healthd::mojom::EventObserver> receiver_{this};

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
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_EVENT_SUBSCRIBER_H_
