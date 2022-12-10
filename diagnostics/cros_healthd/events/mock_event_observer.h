// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_MOCK_EVENT_OBSERVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_MOCK_EVENT_OBSERVER_H_

#include <utility>

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

class MockEventObserver : public ::ash::cros_healthd::mojom::EventObserver {
 public:
  explicit MockEventObserver(
      mojo::PendingReceiver<::ash::cros_healthd::mojom::EventObserver> receiver)
      : receiver_{this /* impl */, std::move(receiver)} {
    DCHECK(receiver_.is_bound());
  }
  MockEventObserver(const MockEventObserver&) = delete;
  MockEventObserver& operator=(const MockEventObserver&) = delete;

  MOCK_METHOD(void,
              OnEvent,
              (::ash::cros_healthd::mojom::EventInfoPtr),
              (override));

 private:
  mojo::Receiver<::ash::cros_healthd::mojom::EventObserver> receiver_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_MOCK_EVENT_OBSERVER_H_
