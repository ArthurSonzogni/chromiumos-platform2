// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_THUNDERBOLT_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_THUNDERBOLT_SUBSCRIBER_H_

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// This class subscribes to cros_healthd's Thunderbolt notifications and
// outputs received notifications to stdout.
class ThunderboltSubscriber final
    : public chromeos::cros_healthd::mojom::CrosHealthdThunderboltObserver {
 public:
  explicit ThunderboltSubscriber(
      mojo::PendingReceiver<
          chromeos::cros_healthd::mojom::CrosHealthdThunderboltObserver>
          receiver);
  ThunderboltSubscriber(const ThunderboltSubscriber&) = delete;
  ThunderboltSubscriber& operator=(const ThunderboltSubscriber&) = delete;
  ~ThunderboltSubscriber();

  // chromeos::cros_healthd::mojom::CrosHealthdThunderboltObserver overrides:
  void OnAdd() override;
  void OnRemove() override;
  void OnAuthorized() override;
  void OnUnAuthorized() override;

 private:
  // Allows the remote cros_healthd to call ThunderboltSubscriber's
  // CrosHealthdThunderboltObserver methods.
  const mojo::Receiver<
      chromeos::cros_healthd::mojom::CrosHealthdThunderboltObserver>
      receiver_;
};

}  // namespace diagnostics
#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_THUNDERBOLT_SUBSCRIBER_H_
