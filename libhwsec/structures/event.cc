// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/structures/event.h"

#include <optional>
#include <utility>

#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"

namespace hwsec {

ScopedEvent::ScopedEvent(ScopedEvent&& scoped_event)
    : event_(std::move(scoped_event.event_)),
      middleware_derivative_(std::move(scoped_event.middleware_derivative_)) {
  scoped_event.event_.clear();
}

ScopedEvent::~ScopedEvent() {
  Stop();
}

void ScopedEvent::Stop() {
  if (!event_.empty()) {
    std::string event(std::move(event_));
    event_.clear();

    // Using async stop if we have task runner on the current thread to improve
    // the performance.
    if (base::SequencedTaskRunner::HasCurrentDefault()) {
      base::OnceCallback<void(hwsec::Status)> callback = base::BindOnce(
          [](const std::string& event, hwsec::Status result) {
            if (!result.ok()) {
              LOG(ERROR) << "Failed to stop event(" << event << "): " << result;
            }
          },
          event);
      Middleware(middleware_derivative_)
          .CallAsync<&hwsec::Backend::EventManagement::Stop>(
              std::move(callback), event);
    } else {
      RETURN_IF_ERROR(
          Middleware(middleware_derivative_)
              .CallSync<&hwsec::Backend::EventManagement::Stop>(event))
          .With([event](auto linker) {
            return linker.LogError() << "Failed to stop event(" << event << ")";
          })
          .ReturnVoid();
    }
  }
}

ScopedEvent& ScopedEvent::operator=(ScopedEvent&& scoped_event) {
  Stop();
  event_ = std::move(scoped_event.event_);
  middleware_derivative_ = std::move(scoped_event.middleware_derivative_);
  scoped_event.event_.clear();
  return *this;
}

ScopedEvent::ScopedEvent(const std::string& event,
                         MiddlewareDerivative middleware_derivative)
    : event_(event), middleware_derivative_(std::move(middleware_derivative)) {}

}  // namespace hwsec
