// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/memory/scoped_ptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "gobi-cromo-plugin/metrics_stopwatch.h"

using ::testing::AllOf;
using ::testing::Gt;
using ::testing::Lt;
using ::testing::StrEq;
using ::testing::_;

class MetricsStopwatchTest : public ::testing::Test {
 public:
  MetricsStopwatchTest() :
      s("Test", 0, 2000, 5) {
  }
  void SetUp() {
    metrics_ = new MetricsLibraryMock;
    s.SetMetrics(metrics_);
  }

  MetricsLibraryMock *metrics_;
  MetricsStopwatch s;
};

TEST_F(MetricsStopwatchTest, MetricsStopwatchSleep) {
  const int kTarget = 250;
  EXPECT_CALL(*metrics_, SendToUMA(StrEq("Test"),
                                   AllOf(Gt(kTarget / 3), Lt(kTarget * 3)),
                                   0,
                                   2000,
                                   5));
  s.Start();
  usleep(kTarget * 1000);
  s.Stop();
}

TEST_F(MetricsStopwatchTest, SetRegularOrder) {
  EXPECT_CALL(*metrics_, SendToUMA(StrEq("Test"),
                                   75,
                                   0,
                                   2000,
                                   5));
  s.SetStart(1ULL << 32);
  s.SetStop((1ULL << 32) + 75);
}


TEST_F(MetricsStopwatchTest, SetBackwardsAndReset) {
  EXPECT_CALL(*metrics_, SendToUMA(StrEq("Test"),
                                   75,
                                   0,
                                   2000,
                                   5));

  s.SetStart(1);
  s.Reset();
  s.SetStop((1ULL << 32) + 75);
  s.SetStart(1ULL << 32);
}

TEST_F(MetricsStopwatchTest, OnlyStop) {
  // Don't expect any calls
  s.Stop();
}

TEST_F(MetricsStopwatchTest, OnlyStopIfStarted) {
  // Don't expect any calls
  s.StopIfStarted();
}

TEST_F(MetricsStopwatchTest, StopIfStarted) {
  EXPECT_CALL(*metrics_, SendToUMA(StrEq("Test"),
                                   _,
                                   0,
                                   2000,
                                   5));
  s.Start();
  s.StopIfStarted();
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
