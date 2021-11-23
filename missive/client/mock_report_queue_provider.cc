// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/mock_report_queue_provider.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/no_destructor.h>
#include <base/sequenced_task_runner.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "missive/client/mock_report_queue.h"
#include "missive/client/report_queue.h"
#include "missive/client/report_queue_configuration.h"
#include "missive/client/report_queue_provider.h"
#include "missive/storage/test_storage_module.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace reporting {

MockReportQueueProvider::MockReportQueueProvider()
    : ReportQueueProvider(base::BindRepeating(
          [](OnStorageModuleCreatedCallback storage_created_cb) {
            std::move(storage_created_cb)
                .Run(base::MakeRefCounted<test::TestStorageModule>());
          })) {}
MockReportQueueProvider::~MockReportQueueProvider() = default;

void MockReportQueueProvider::ExpectCreateNewQueueAndReturnNewMockQueue(
    size_t times) {
  EXPECT_CALL(*this, CreateNewQueue(_, _))
      .Times(times)
      .WillRepeatedly([](std::unique_ptr<ReportQueueConfiguration> config,
                         CreateReportQueueCallback cb) {
        std::move(cb).Run(std::make_unique<MockReportQueue>());
      });
}

void MockReportQueueProvider::
    ExpectCreateNewSpeculativeQueueAndReturnNewMockQueue(size_t times) {
  // Mock internals so we do not unnecessarily create a new report queue.
  EXPECT_CALL(*this, CreateNewQueue(_, _))
      .Times(times)
      .WillRepeatedly([](std::unique_ptr<ReportQueueConfiguration> config,
                         CreateReportQueueCallback cb) {
        std::move(cb).Run(std::unique_ptr<ReportQueue>(nullptr));
      });

  EXPECT_CALL(*this, CreateNewSpeculativeQueue())
      .Times(times)
      .WillRepeatedly([]() {
        auto report_queue =
            std::unique_ptr<MockReportQueue, base::OnTaskRunnerDeleter>(
                new NiceMock<MockReportQueue>(),
                base::OnTaskRunnerDeleter(
                    base::ThreadPool::CreateSequencedTaskRunner({})));

        // Mock PrepareToAttachActualQueue so we do not attempt to replace
        // the mocked report queue
        EXPECT_CALL(*report_queue, PrepareToAttachActualQueue()).WillOnce([]() {
          return base::DoNothing();
        });

        return report_queue;
      });
}

}  // namespace reporting
