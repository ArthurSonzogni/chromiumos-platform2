// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_EVENT_DISPATCHER_H_
#define SHILL_MOCK_EVENT_DISPATCHER_H_

#include <base/location.h>
#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/event_dispatcher.h"

namespace shill {

class MockEventDispatcher : public EventDispatcher {
 public:
  MockEventDispatcher();
  ~MockEventDispatcher() override;

  MOCK_METHOD(void, DispatchForever, (), (override));
  MOCK_METHOD(void, DispatchPendingEvents, (), (override));
  MOCK_METHOD(void,
              PostDelayedTask,
              (const base::Location&, const base::Closure&, int64_t),
              (override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockEventDispatcher);
};

}  // namespace shill

#endif  // SHILL_MOCK_EVENT_DISPATCHER_H_
