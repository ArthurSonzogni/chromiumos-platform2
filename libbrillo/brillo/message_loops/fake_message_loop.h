// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_MESSAGE_LOOPS_FAKE_MESSAGE_LOOP_H_
#define LIBBRILLO_BRILLO_MESSAGE_LOOPS_FAKE_MESSAGE_LOOP_H_

#include <functional>
#include <map>
#include <queue>
#include <set>
#include <utility>
#include <vector>

#include <base/location.h>
#include <base/test/simple_test_clock.h>
#include <base/time/time.h>

#include <brillo/brillo_export.h>
#include <brillo/message_loops/message_loop.h>

namespace brillo {

// The FakeMessageLoop implements a message loop that doesn't block or wait for
// time based tasks to be ready. The tasks are executed in the order they should
// be executed in a real message loop implementation, but the time is advanced
// to the time when the first task should be executed instead of blocking.
// To keep a consistent notion of time for other classes, FakeMessageLoop
// optionally updates a SimpleTestClock instance when it needs to advance the
// clock.
// This message loop implementation is useful for unittests.
class BRILLO_EXPORT FakeMessageLoop : public MessageLoop {
 public:
  // Create a FakeMessageLoop optionally using a SimpleTestClock to update the
  // time when Run() or RunOnce(true) are called and should block.
  explicit FakeMessageLoop(base::SimpleTestClock* clock);
  FakeMessageLoop(const FakeMessageLoop&) = delete;
  FakeMessageLoop& operator=(const FakeMessageLoop&) = delete;

  ~FakeMessageLoop() override = default;

  TaskId PostDelayedTask(const base::Location& from_here,
                         base::OnceClosure task,
                         base::TimeDelta delay) override;
  using MessageLoop::PostDelayedTask;
  bool CancelTask(TaskId task_id) override;
  bool RunOnce(bool may_block) override;

  // FakeMessageLoop methods:

  // Returns whether there are pending tasks. Useful to check that no
  // callbacks were leaked.
  bool PendingTasks();

 private:
  struct ScheduledTask {
    base::Location location;
    base::OnceClosure callback;
  };

  // The sparse list of scheduled pending callbacks.
  std::map<MessageLoop::TaskId, ScheduledTask> tasks_;

  // Using std::greater<> for the priority_queue means that the top() of the
  // queue is the lowest (earliest) time, and for the same time, the smallest
  // TaskId. This determines the order in which the tasks will be fired.
  std::priority_queue<std::pair<base::Time, MessageLoop::TaskId>,
                      std::vector<std::pair<base::Time, MessageLoop::TaskId>>,
                      std::greater<std::pair<base::Time, MessageLoop::TaskId>>>
      fire_order_;

  base::SimpleTestClock* test_clock_ = nullptr;
  base::Time current_time_ = base::Time::FromSecondsSinceUnixEpoch(1246996800.);

  MessageLoop::TaskId last_id_ = kTaskIdNull;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_MESSAGE_LOOPS_FAKE_MESSAGE_LOOP_H_
