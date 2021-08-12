// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_USB_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_USB_SUBSCRIBER_H_

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

// This class subscribes to cros_healthd's USB notifications and outputs
// received notifications to stdout.
class UsbSubscriber final
    : public chromeos::cros_healthd::mojom::CrosHealthdUsbObserver {
 public:
  explicit UsbSubscriber(
      mojo::PendingReceiver<
          chromeos::cros_healthd::mojom::CrosHealthdUsbObserver> receiver);
  UsbSubscriber(const UsbSubscriber&) = delete;
  UsbSubscriber& operator=(const UsbSubscriber&) = delete;
  ~UsbSubscriber();

  // chromeos::cros_healthd::mojom::CrosHealthdUsbObserver overrides:
  void OnAdd(
      const chromeos::cros_healthd::mojom::UsbEventInfoPtr info) override;
  void OnRemove(
      const chromeos::cros_healthd::mojom::UsbEventInfoPtr info) override;

 private:
  // Allows the remote cros_healthd to call UsbSubscriber's
  // CrosHealthdUsbObserver methods.
  const mojo::Receiver<chromeos::cros_healthd::mojom::CrosHealthdUsbObserver>
      receiver_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_USB_SUBSCRIBER_H_
