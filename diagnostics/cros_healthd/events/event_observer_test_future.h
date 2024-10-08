// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_EVENT_OBSERVER_TEST_FUTURE_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_EVENT_OBSERVER_TEST_FUTURE_H_

#include <utility>

#include <base/check.h>
#include <base/test/test_future.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

// Helper class to test event implementation. It will store the received events
// and these events can be popped in a FIFO fashion via |WaitForEvent()|.
//
// Example usage:
//
//   TEST_F(ExampleTestFixture, ExampleEvent) {
//     EventObserverTestFuture event_observer;
//     event_source_.AddObserver(event_observer.BindNewPendingRemote());
//
//     EmitExampleEvent();
//
//     auto event = event_observer.WaitForEvent();
//     EXPECT_THAT(event, some_matcher);
//   }
class EventObserverTestFuture : public ash::cros_healthd::mojom::EventObserver {
 public:
  EventObserverTestFuture() = default;
  EventObserverTestFuture(const EventObserverTestFuture&) = delete;
  EventObserverTestFuture& operator=(const EventObserverTestFuture&) = delete;
  ~EventObserverTestFuture() = default;

  // ash::cros_healthd::mojom::EventObserver overrides:
  void OnEvent(ash::cros_healthd::mojom::EventInfoPtr event) override {
    CHECK(!event_.IsReady())
        << "EventObserverTestFuture cannot store multiple events";
    event_.SetValue(std::move(event));
  }

  mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver>
  BindNewPendingRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  ash::cros_healthd::mojom::EventInfoPtr WaitForEvent() {
    return event_.Take();
  }

  // Reset the underlying receiver.
  void Reset() { receiver_.reset(); }

 private:
  mojo::Receiver<ash::cros_healthd::mojom::EventObserver> receiver_{this};
  base::test::TestFuture<ash::cros_healthd::mojom::EventInfoPtr> event_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_EVENT_OBSERVER_TEST_FUTURE_H_
