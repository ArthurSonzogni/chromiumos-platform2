// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_STRUCTURES_EVENT_H_
#define LIBHWSEC_STRUCTURES_EVENT_H_

#include <string>

#include "libhwsec/hwsec_export.h"
#include "libhwsec/middleware/middleware_derivative.h"
#include "libhwsec/structures/no_default_init.h"

namespace hwsec {

class HWSEC_EXPORT ScopedEvent {
 public:
  ScopedEvent() = default;
  ScopedEvent(ScopedEvent&& scoped_event);
  ScopedEvent(const ScopedEvent& scoped_event) = delete;
  ScopedEvent(const std::string& event,
              MiddlewareDerivative middleware_derivative);
  ~ScopedEvent();

  ScopedEvent& operator=(ScopedEvent&& scoped_event);
  ScopedEvent& operator=(const ScopedEvent& scoped_event) = delete;

  // Stops the event.
  void Stop();

 private:
  std::string event_;
  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_STRUCTURES_EVENT_H_
