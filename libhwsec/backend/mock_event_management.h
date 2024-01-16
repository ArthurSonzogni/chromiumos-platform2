// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_EVENT_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_MOCK_EVENT_MANAGEMENT_H_

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec/backend/event_management.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/event.h"

namespace hwsec {

class BackendTpm2;

class MockEventManagement : public EventManagement {
 public:
  MockEventManagement() = default;
  explicit MockEventManagement(EventManagement* on_call) : default_(on_call) {
    using testing::Invoke;
    if (!default_)
      return;
    ON_CALL(*this, Start)
        .WillByDefault(Invoke(default_, &EventManagement::Start));
    ON_CALL(*this, Stop)
        .WillByDefault(Invoke(default_, &EventManagement::Stop));
  }

  MOCK_METHOD(StatusOr<ScopedEvent>,
              Start,
              (const std::string& event),
              (override));
  MOCK_METHOD(Status, Stop, (const std::string& event), (override));

 private:
  EventManagement* default_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_EVENT_MANAGEMENT_H_
