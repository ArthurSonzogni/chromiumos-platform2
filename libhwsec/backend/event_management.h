// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_EVENT_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_EVENT_MANAGEMENT_H_

#include <string>

#include "libhwsec/status.h"
#include "libhwsec/structures/event.h"

namespace hwsec {

// EventManagement provides the functions to manage events.
class EventManagement {
 public:
  // Starts a |event|, the event will automatically stop when the ScopedEvent is
  // destroyed.
  virtual StatusOr<ScopedEvent> Start(const std::string& event) = 0;

  // Stops a |event|.
  virtual Status Stop(const std::string& event) = 0;

 protected:
  EventManagement() = default;
  ~EventManagement() = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_EVENT_MANAGEMENT_H_
