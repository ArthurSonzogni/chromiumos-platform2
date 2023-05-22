// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_UTIL_TEST_SUPPORT_CALLBACKS_H_
#define MISSIVE_UTIL_TEST_SUPPORT_CALLBACKS_H_

#include <optional>
#include <tuple>
#include <utility>

#include <base/atomic_ref_count.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include "base/location.h"
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/run_loop.h>
#include <base/synchronization/lock.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include "base/test/repeating_test_future.h"
#include <base/test/test_future.h>
#include <base/thread_annotations.h>
#include <gtest/gtest.h>

namespace reporting::test {

// Usage (in tests only):
//
//  TestCallbackWaiter waiter;
//  ... do something
//  waiter.Wait();
//
//  or, with multithreadeded activity:
//
//  TestCallbackWaiter waiter;
//  waiter.Attach(N);  // N - is a number of asynchronous actions
//  ...
//  waiter.Wait();
//
//  And  in each of N actions: waiter.Signal(); when done

class TestCallbackWaiter {
 public:
  TestCallbackWaiter();
  ~TestCallbackWaiter();
  TestCallbackWaiter(const TestCallbackWaiter& other) = delete;
  TestCallbackWaiter& operator=(const TestCallbackWaiter& other) = delete;

  void Attach(int more = 1) {
    const int old_counter = counter_.Increment(more);
    DCHECK_GT(old_counter, 0) << "Cannot attach when already being released";
  }

  void Signal() {
    if (counter_.Decrement()) {
      // There are more owners.
      return;
    }
    // Dropping the last owner.
    sequenced_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&base::test::TestFuture<void>::SetValue,
                                  test_future_weak_ptr_factory_.GetWeakPtr()));
  }

  void Wait() {
    Signal();  // Rid of the constructor's ownership.
    ASSERT_TRUE(test_future_.Wait());
  }

 private:
  base::AtomicRefCount counter_{1};  // Owned by constructor.
  base::test::TestFuture<void> test_future_;
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  base::WeakPtrFactory<base::test::TestFuture<void>>
      test_future_weak_ptr_factory_{&test_future_};
};

// RAII wrapper for TestCallbackWaiter.
//
// Usage:
// {
//   TestCallbackAutoWaiter waiter;  // Implicitly Attach(1);
//   ...
//   Launch async activity, which will eventually do waiter.Signal();
//   ...
// }   // Here the waiter will automatically wait.

class TestCallbackAutoWaiter : public TestCallbackWaiter {
 public:
  TestCallbackAutoWaiter();
  ~TestCallbackAutoWaiter();
};

}  // namespace reporting::test

#endif  // MISSIVE_UTIL_TEST_SUPPORT_CALLBACKS_H_
