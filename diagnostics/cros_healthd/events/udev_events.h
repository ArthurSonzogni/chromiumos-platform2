// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_H_

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

// Interface which allows clients to subscribe to udev-related events.
class UdevEvents {
 public:
  UdevEvents() = default;
  UdevEvents(const UdevEvents&) = delete;
  UdevEvents& operator=(const UdevEvents&) = delete;
  virtual ~UdevEvents() = default;

  virtual void Initialize() = 0;
  // Adds a new observer to be notified when thunderbolt related events occur.
  virtual void AddThunderboltObserver(
      mojo::PendingRemote<
          chromeos::cros_healthd::mojom::CrosHealthdThunderboltObserver>
          observer) = 0;
  // Adds a new observer to be notified when USB related events occur.
  virtual void AddUsbObserver(
      mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdUsbObserver>
          observer) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_H_
