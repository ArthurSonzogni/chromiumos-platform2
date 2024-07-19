// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TOUCHPAD_EVDEV_DELEGATE_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TOUCHPAD_EVDEV_DELEGATE_H_

#include <linux/input.h>

#include <cstdint>
#include <string>

#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/delegate/utils/evdev_monitor.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics {

class LibevdevWrapper;

class TouchpadEvdevDelegate final : public EvdevMonitor::Delegate {
 public:
  explicit TouchpadEvdevDelegate(
      mojo::PendingRemote<ash::cros_healthd::mojom::TouchpadObserver> observer);

  // EvdevMonitor::Delegate overrides.
  bool IsTarget(LibevdevWrapper* dev) override;
  void FireEvent(const input_event& event, LibevdevWrapper* dev) override;
  void InitializationFail(uint32_t custom_reason,
                          const std::string& description) override;
  void ReportProperties(LibevdevWrapper* dev) override;

 private:
  mojo::Remote<ash::cros_healthd::mojom::TouchpadObserver> observer_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_EVENTS_TOUCHPAD_EVDEV_DELEGATE_H_
