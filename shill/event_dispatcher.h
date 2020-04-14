// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_EVENT_DISPATCHER_H_
#define SHILL_EVENT_DISPATCHER_H_

#include <stdint.h>

#include <base/callback.h>
#include <base/location.h>
#include <base/macros.h>

namespace shill {

// This is the main event dispatcher.  It contains a central instance, and is
// the entity responsible for dispatching events out of all queues to their
// listeners during the idle loop.
class EventDispatcher {
 public:
  EventDispatcher();
  virtual ~EventDispatcher();

  virtual void DispatchForever();

  // Processes all pending events that can run and returns.
  virtual void DispatchPendingEvents();

  // These are thin wrappers around calls of the same name in
  // <base/message_loop_proxy.h>
  void PostTask(const base::Location& location, const base::Closure& task);
  virtual void PostDelayedTask(const base::Location& location,
                               const base::Closure& task,
                               int64_t delay_ms);

  virtual void QuitDispatchForever();

 private:
  base::RepeatingClosure quit_closure_;
  DISALLOW_COPY_AND_ASSIGN(EventDispatcher);
};

}  // namespace shill

#endif  // SHILL_EVENT_DISPATCHER_H_
