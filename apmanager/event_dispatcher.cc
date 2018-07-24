// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apmanager/event_dispatcher.h"

#include <base/location.h>
#include <base/message_loop/message_loop_proxy.h>
#include <base/time/time.h>

namespace apmanager {

namespace {

base::LazyInstance<EventDispatcher> g_event_dispatcher
    = LAZY_INSTANCE_INITIALIZER;

}  // namespace

EventDispatcher::EventDispatcher() {}
EventDispatcher::~EventDispatcher() {}

EventDispatcher* EventDispatcher::GetInstance() {
  return g_event_dispatcher.Pointer();
}

bool EventDispatcher::PostTask(const base::Closure& task) {
  return base::MessageLoopProxy::current()->PostTask(FROM_HERE, task);
}

bool EventDispatcher::PostDelayedTask(const base::Closure& task,
                                      int64_t delay_ms) {
  return base::MessageLoopProxy::current()->PostDelayedTask(
      FROM_HERE, task, base::TimeDelta::FromMilliseconds(delay_ms));
}

}  // namespace apmanager
