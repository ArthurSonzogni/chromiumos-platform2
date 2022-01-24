// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/mock_report_queue_provider.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/sequenced_task_runner.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <base/threading/thread_task_runner_handle.h>
#include <gmock/gmock.h>

#include "missive/client/mock_report_queue.h"
#include "missive/client/report_queue.h"
#include "missive/client/report_queue_configuration.h"
#include "missive/client/report_queue_provider.h"
#include "missive/storage/test_storage_module.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::WithArg;

namespace reporting {

MockReportQueueProvider::MockReportQueueProvider()
    : ReportQueueProvider(base::BindRepeating(
          [](OnStorageModuleCreatedCallback storage_created_cb) {
            std::move(storage_created_cb)
                .Run(base::MakeRefCounted<test::TestStorageModule>());
          })),
      test_sequenced_task_runner_(base::ThreadTaskRunnerHandle::Get()) {}

MockReportQueueProvider::~MockReportQueueProvider() = default;

void MockReportQueueProvider::ExpectCreateNewQueueAndReturnNewMockQueue(
    size_t times) {
  CheckOnThread();

  EXPECT_CALL(*this, CreateNewQueueMock(_, _))
      .Times(times)
      .WillRepeatedly([](std::unique_ptr<ReportQueueConfiguration> config,
                         CreateReportQueueCallback cb) {
        std::move(cb).Run(std::make_unique<MockReportQueue>());
      });
}

void MockReportQueueProvider::
    ExpectCreateNewSpeculativeQueueAndReturnNewMockQueue(size_t times) {
  CheckOnThread();

  // Mock internals so we do not unnecessarily create a new report queue.
  EXPECT_CALL(*this, CreateNewQueueMock(_, _))
      .Times(times)
      .WillRepeatedly(WithArg<1>(Invoke([](CreateReportQueueCallback cb) {
        std::move(cb).Run(std::unique_ptr<ReportQueue>(nullptr));
      })));

  EXPECT_CALL(*this, CreateNewSpeculativeQueueMock())
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

void MockReportQueueProvider::OnInitCompleted() {
  // OnInitCompleted is called on a thread pool, so in order to make potential
  // EXPECT_CALLs happen sequentially, we assign Mock to the test's main thread
  // task runner.
  test_sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&MockReportQueueProvider::OnInitCompletedMock,
                                base::Unretained(this)));
}

void MockReportQueueProvider::CreateNewQueue(
    std::unique_ptr<ReportQueueConfiguration> config,
    CreateReportQueueCallback cb) {
  // CreateNewQueue is called on a thread pool, so in order to make potential
  // EXPECT_CALLs happen sequentially, we assign Mock to the test's main thread
  // task runner.
  test_sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&MockReportQueueProvider::CreateNewQueueMock,
                     base::Unretained(this), std::move(config), std::move(cb)));
}

StatusOr<std::unique_ptr<ReportQueue, base::OnTaskRunnerDeleter>>
MockReportQueueProvider::CreateNewSpeculativeQueue() {
  CheckOnThread();
  return CreateNewSpeculativeQueueMock();
}

void MockReportQueueProvider::ConfigureReportQueue(
    std::unique_ptr<ReportQueueConfiguration> report_queue_config,
    ReportQueueConfiguredCallback completion_cb) {
  // ConfigureReportQueue is called on a thread pool, so in order to make
  // potential EXPECT_CALLs happen sequentially, we assign Mock to the test's
  // main thread task runner.
  test_sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&MockReportQueueProvider::ConfigureReportQueueMock,
                     base::Unretained(this), std::move(report_queue_config),
                     std::move(completion_cb)));
}

void MockReportQueueProvider::CheckOnThread() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(test_sequence_checker_);
}
}  // namespace reporting
