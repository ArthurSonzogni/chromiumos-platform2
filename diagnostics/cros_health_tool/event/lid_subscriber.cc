// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/lid_subscriber.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>

namespace diagnostics {

namespace {

constexpr char kHumanReadableOnLidClosedEvent[] = "Lid closed";
constexpr char kHumanReadableOnLidOpenedEvent[] = "Lid opened";

// Enumeration of the different lid event types.
enum class LidEventType {
  kOnLidClosed,
  kOnLidOpened,
};

std::string GetHumanReadableEvent(LidEventType event) {
  switch (event) {
    case LidEventType::kOnLidClosed:
      return kHumanReadableOnLidClosedEvent;
    case LidEventType::kOnLidOpened:
      return kHumanReadableOnLidOpenedEvent;
  }
}

void PrintLidNotification(LidEventType event) {
  std::cout << "Lid event received: " << GetHumanReadableEvent(event)
            << std::endl;
}

}  // namespace

LidSubscriber::LidSubscriber(
    mojo::PendingReceiver<chromeos::cros_healthd::mojom::CrosHealthdLidObserver>
        receiver)
    : receiver_{this /* impl */, std::move(receiver)} {
  DCHECK(receiver_.is_bound());
}

LidSubscriber::~LidSubscriber() = default;

void LidSubscriber::OnLidClosed() {
  PrintLidNotification(LidEventType::kOnLidClosed);
}

void LidSubscriber::OnLidOpened() {
  PrintLidNotification(LidEventType::kOnLidOpened);
}

}  // namespace diagnostics
