// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/report_queue.h"

#include <base/bind.h>
#include <base/strings/strcat.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/client/mock_report_queue.h"
#include "missive/proto/record.pb.h"
#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/statusor.h"
#include "missive/util/test_support_callbacks.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

namespace reporting {
namespace {

class ReportQueueTest : public ::testing::Test {
 protected:
  ReportQueueTest() = default;

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(ReportQueueTest, EnqueueTest) {
  MockReportQueue queue;
  EXPECT_CALL(queue, AddRecord(_, _, _))
      .WillOnce(WithArg<2>(Invoke([](ReportQueue::EnqueueCallback cb) {
        std::move(cb).Run(Status::StatusOK());
      })));
  test::TestEvent<Status> e;
  queue.Enqueue("Record", FAST_BATCH, e.cb());
  ASSERT_OK(e.result());
}

TEST_F(ReportQueueTest, FlushTest) {
  MockReportQueue queue;
  EXPECT_CALL(queue, Flush(_, _))
      .WillOnce(WithArg<1>(Invoke([](ReportQueue::FlushCallback cb) {
        std::move(cb).Run(Status::StatusOK());
      })));
  test::TestEvent<Status> e;
  queue.Flush(MANUAL_BATCH, e.cb());
  ASSERT_OK(e.result());
}

}  // namespace
}  // namespace reporting
