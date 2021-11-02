// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec-foundation/utility/synchronized.h"

namespace hwsec_foundation {
namespace utility {

namespace {

class ThreadUnsafeCounter {
 public:
  ThreadUnsafeCounter() {}

  void Update(int n) {
    int old = value_;
    int multiplier = 1;
    for (int i = 0; i < n; i++) {
      multiplier = multiplier * kMultiplier % kModulo;
      ++updated_times_;
      // Sleep so that race condition will happen with higher probability.
      base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1));
    }
    value_ = old * multiplier % kModulo;
  }

  void Reset() {
    value_ = 1;
    updated_times_ = 0;
  }

  int value() { return value_; }

  int updated_times() { return updated_times_; }

 private:
  const int kMultiplier = 37, kModulo = 1003;

  int value_ = 1;
  int updated_times_ = 0;
};

class UpdateCounterThread : public base::PlatformThread::Delegate {
 public:
  UpdateCounterThread(Synchronized<ThreadUnsafeCounter>* counter, int times)
      : counter_(counter), times_(times) {}
  UpdateCounterThread(ThreadUnsafeCounter* counter, int times)
      : raw_counter_(counter), times_(times) {}

  UpdateCounterThread(const UpdateCounterThread&) = delete;
  UpdateCounterThread& operator=(const UpdateCounterThread&) = delete;

  ~UpdateCounterThread() {}

  void ThreadMain() {
    if (counter_) {
      counter_->Lock()->Update(times_);
    } else if (raw_counter_) {
      raw_counter_->Update(times_);
    }
  }

 private:
  Synchronized<ThreadUnsafeCounter>* counter_ = nullptr;
  ThreadUnsafeCounter* raw_counter_ = nullptr;
  int times_;
};

struct ThreadInfo {
  base::PlatformThreadHandle handle;
  std::unique_ptr<UpdateCounterThread> thread;
};

}  // namespace

class SynchronizedUtilityTest : public testing::Test {
 public:
  ~SynchronizedUtilityTest() override = default;

  void SetUp() override {}
};

TEST_F(SynchronizedUtilityTest, Trivial) {
  Synchronized<std::string> str("Hello");

  EXPECT_EQ(str.Lock()->length(), 5);

  str.Lock()->push_back('!');
  EXPECT_EQ(str.Lock()->length(), 6);
}

TEST_F(SynchronizedUtilityTest, ThreadSafeAccess) {
  Synchronized<ThreadUnsafeCounter> counter;

  for (int i = 0; i < 10; i++) {
    counter.Lock()->Update(1000);
  }
  int single_thread_result = counter.Lock()->value();

  counter.Lock()->Reset();

  std::vector<ThreadInfo> thread_infos(10);
  for (int i = 0; i < 10; i++) {
    thread_infos[i].thread.reset(new UpdateCounterThread(&counter, 1000));
    base::PlatformThread::Create(0, thread_infos[i].thread.get(),
                                 &thread_infos[i].handle);
  }
  for (auto& thread_info : thread_infos) {
    base::PlatformThread::Join(thread_info.handle);
  }

  EXPECT_EQ(single_thread_result, counter.Lock()->value());
}

TEST_F(SynchronizedUtilityTest, ThreadSafeCriticalSection) {
  Synchronized<ThreadUnsafeCounter> counter;

  std::vector<ThreadInfo> thread_infos(10);
  for (int i = 0; i < 10; i++) {
    thread_infos[i].thread.reset(new UpdateCounterThread(&counter, 1000));
    base::PlatformThread::Create(0, thread_infos[i].thread.get(),
                                 &thread_infos[i].handle);
  }

  bool success;
  {
    auto handle = counter.Lock();
    int updated_times = handle->updated_times();
    handle->Update(100);
    success = (updated_times + 100 == handle->updated_times());
  }

  for (auto& thread_info : thread_infos) {
    base::PlatformThread::Join(thread_info.handle);
  }

  EXPECT_TRUE(success);
}

class SynchronizedUtilityRaceConditionTest : public testing::Test {
 public:
  ~SynchronizedUtilityRaceConditionTest() override = default;

  void SetUp() override {
    // These race condition tests are for ensuring the parameters used in the
    // tests for the Synchronized wrapper will cause race conditions and fail
    // the checks, if no synchronization mechanisms were used. We skip these
    // tests because their results are probabilistic.
    GTEST_SKIP();
  }
};

TEST_F(SynchronizedUtilityRaceConditionTest, ThreadUnsafeAccess) {
  ThreadUnsafeCounter counter;

  for (int i = 0; i < 10; i++) {
    counter.Update(1000);
  }
  int single_thread_result = counter.value();

  counter.Reset();

  std::vector<ThreadInfo> thread_infos(10);
  for (int i = 0; i < 10; i++) {
    thread_infos[i].thread.reset(new UpdateCounterThread(&counter, 1000));
    base::PlatformThread::Create(0, thread_infos[i].thread.get(),
                                 &thread_infos[i].handle);
  }
  for (auto& thread_info : thread_infos) {
    base::PlatformThread::Join(thread_info.handle);
  }

  int multi_thread_result = counter.value();

  EXPECT_NE(single_thread_result, multi_thread_result);
}

TEST_F(SynchronizedUtilityRaceConditionTest, ThreadUnsafeCriticalSection) {
  ThreadUnsafeCounter counter;

  std::vector<ThreadInfo> thread_infos(10);
  for (int i = 0; i < 10; i++) {
    thread_infos[i].thread.reset(new UpdateCounterThread(&counter, 1000));
    base::PlatformThread::Create(0, thread_infos[i].thread.get(),
                                 &thread_infos[i].handle);
  }

  int updated_times = counter.updated_times();
  counter.Update(1000);
  bool success = (updated_times + 1000 == counter.updated_times());

  for (auto& thread_info : thread_infos) {
    base::PlatformThread::Join(thread_info.handle);
  }

  EXPECT_FALSE(success);
}

}  // namespace utility
}  // namespace hwsec_foundation
