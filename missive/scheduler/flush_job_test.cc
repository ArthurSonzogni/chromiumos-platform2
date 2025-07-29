// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/flush_job.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/run_loop.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/test/task_environment.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/health/health_module.h"
#include "missive/health/health_module_delegate_mock.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/test_support_callbacks.h"
#include "missive/util/test_util.h"

namespace reporting {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::StrEq;
using ::testing::WithArgs;

class MockStorageModule : public StorageModuleInterface {
 public:
  static scoped_refptr<MockStorageModule> Create() {
    return base::WrapRefCounted(new MockStorageModule());
  }

  MOCK_METHOD(void,
              AddRecord,
              (Priority, Record, base::OnceCallback<void(Status)>),
              (override));
  MOCK_METHOD(void,
              Flush,
              (Priority, base::OnceCallback<void(Status)>),
              (override));
};

class FlushJobTest : public ::testing::Test {
 public:
  FlushJobTest() = default;

 protected:
  void SetUp() override {
    response_ = std::make_unique<
        brillo::dbus_utils::MockDBusMethodResponse<FlushPriorityResponse>>();

    priority_ = Priority::FAST_BATCH;

    health_module_ =
        HealthModule::Create(std::make_unique<HealthModuleDelegateMock>());
    storage_module_ = MockStorageModule::Create();
  }

  void TearDown() override {
    // Let everything ongoing to finish.
    task_environment_.RunUntilIdle();
  }

  base::test::TaskEnvironment task_environment_;

  std::unique_ptr<
      brillo::dbus_utils::MockDBusMethodResponse<FlushPriorityResponse>>
      response_;
  Priority priority_;

  scoped_refptr<MockStorageModule> storage_module_;

  scoped_refptr<HealthModule> health_module_;
};

TEST_F(FlushJobTest, CompletesSuccessfully) {
  response_->set_return_callback(
      base::BindOnce([](const FlushPriorityResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(error::OK));
      }));
  auto delegate = std::make_unique<FlushJob::FlushResponseDelegate>(
      health_module_, std::move(response_));

  FlushPriorityRequest request;
  request.set_priority(priority_);

  EXPECT_CALL(*storage_module_, Flush(Eq(priority_), _))
      .WillOnce(WithArgs<1>(Invoke([](base::OnceCallback<void(Status)> cb) {
        std::move(cb).Run(Status::StatusOK());
      })));

  auto job = FlushJob::Create(storage_module_, health_module_, request,
                              std::move(delegate));

  test::TestEvent<Status> enqueued;
  job->Start(enqueued.cb());
  const Status status = enqueued.result();
  EXPECT_OK(status) << status;
}

TEST_F(FlushJobTest, CancelsSuccessfully) {
  Status failure_status(error::INTERNAL, "Failing for tests");
  response_->set_return_callback(base::BindOnce(
      [](Status failure_status, const FlushPriorityResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(failure_status.error_code()));
        EXPECT_THAT(response.status().error_message(),
                    StrEq(std::string(failure_status.error_message())));
      },
      failure_status));
  auto delegate = std::make_unique<FlushJob::FlushResponseDelegate>(
      health_module_, std::move(response_));

  FlushPriorityRequest request;
  request.set_priority(priority_);

  EXPECT_CALL(*storage_module_, Flush(_, _)).Times(0);

  auto job = FlushJob::Create(storage_module_, health_module_, request,
                              std::move(delegate));

  job->Cancel(failure_status);
}

}  // namespace
}  // namespace reporting
