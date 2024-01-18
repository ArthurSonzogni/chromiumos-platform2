// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_MOCK_SCHEDULER_H_
#define FEDERATED_MOCK_SCHEDULER_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "federated/scheduler.h"

namespace federated {

// A mock Scheduler which fakes Schedule() called by federated_service.
class MockScheduler : public Scheduler {
 public:
  using Scheduler::Scheduler;
  MockScheduler(const MockScheduler&) = delete;
  MockScheduler& operator=(const MockScheduler&) = delete;

  ~MockScheduler() override = default;

  MOCK_METHOD(
      void,
      Schedule,
      (const std::vector<chromeos::federated::mojom::ClientScheduleConfigPtr>&),
      (override));
};

}  // namespace federated

#endif  // FEDERATED_MOCK_SCHEDULER_H_
