// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_H_

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// Interface which allows clients to subscribe to udev-related events.
class UdevEvents {
 public:
  UdevEvents() = default;
  UdevEvents(const UdevEvents&) = delete;
  UdevEvents& operator=(const UdevEvents&) = delete;
  virtual ~UdevEvents() = default;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_H_
