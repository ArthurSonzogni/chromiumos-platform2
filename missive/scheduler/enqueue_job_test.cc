// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/enqueue_job.h"

#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

#include <base/bind.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/storage/storage_module_interface.h"
#include "missive/util/status.h"

namespace reporting {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::WithArgs;

class TestCallbackWaiter {
 public:
  TestCallbackWaiter() : run_loop_(std::make_unique<base::RunLoop>()) {}

  virtual void Signal() { run_loop_->Quit(); }

  void Complete() { Signal(); }

  void Wait() { run_loop_->Run(); }

 protected:
  std::unique_ptr<base::RunLoop> run_loop_;
};

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

class MockStorageModule : public StorageModuleInterface {
 public:
  static scoped_refptr<MockStorageModule> Create() {
    return base::WrapRefCounted(new MockStorageModule());
  }

  MOCK_METHOD(void,
              AddRecord,
              (Priority, Record, base::OnceCallback<void(Status)>),
              (override));

  MOCK_METHOD(void, ReportSuccess, (SequencingInformation, bool), (override));
  MOCK_METHOD(void, UpdateEncryptionKey, (SignedEncryptionInfo), (override));
};

class EnqueueJobTest : public ::testing::Test {
 public:
  EnqueueJobTest() : method_call_("org.Test", "TestMethod") {}

 protected:
  void SetUp() override {
    response_ = std::make_unique<
        brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>(
        &method_call_);
    ASSERT_FALSE(response_->IsResponseSent());

    record_.set_data("TEST_VALUE");
    record_.set_destination(Destination::UPLOAD_EVENTS);
    record_.set_dm_token("TEST_DM_TOKEN");
    record_.set_timestamp_us(1234567);

    fd_ = memfd_create("TempMemFile", 0);
    ASSERT_GE(fd_, 0);
    ASSERT_TRUE(record_.SerializeToFileDescriptor(fd_));

    pid_ = getpid();

    storage_module_ = MockStorageModule::Create();
  }

  void TearDown() override {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  base::test::TaskEnvironment task_environment_;

  dbus::MethodCall method_call_;

  std::unique_ptr<
      brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>
      response_;
  Record record_;
  int fd_;
  pid_t pid_;

  scoped_refptr<MockStorageModule> storage_module_;
};

TEST_F(EnqueueJobTest, CompletesSuccessfully) {
  response_->set_return_callback(
      base::Bind([](const EnqueueRecordResponse& response) {
        LOG(ERROR) << response.status().error_message();
        EXPECT_EQ(response.status().code(), error::OK);
      }));
  auto delegate = std::make_unique<EnqueueJob::EnqueueResponseDelegate>(
      std::move(response_));

  EnqueueRecordRequest request;
  request.set_record_fd(fd_);
  request.set_pid(pid_);
  request.set_priority(Priority::BACKGROUND_BATCH);

  EXPECT_CALL(*storage_module_, AddRecord(Eq(Priority::BACKGROUND_BATCH),
                                          EqualsProto(record_), _))
      .WillOnce(WithArgs<2>(Invoke([](base::OnceCallback<void(Status)> cb) {
        std::move(cb).Run(Status::StatusOK());
      })));

  EnqueueJob job(storage_module_, request, std::move(delegate));

  TestCallbackWaiter waiter;
  job.Start(base::BindOnce(
      [](TestCallbackWaiter* waiter, Status status) {
        EXPECT_TRUE(status.ok());
        waiter->Signal();
      },
      &waiter));
  waiter.Wait();
}

TEST_F(EnqueueJobTest, CancelsSuccessfully) {
  Status failure_status(error::INTERNAL, "Failing for tests");
  response_->set_return_callback(base::Bind(
      [](Status status, const EnqueueRecordResponse& response) {
        EXPECT_TRUE(response.status().code() == status.error_code());
      },
      failure_status));
  auto delegate = std::make_unique<EnqueueJob::EnqueueResponseDelegate>(
      std::move(response_));

  EnqueueRecordRequest request;
  request.set_record_fd(fd_);
  request.set_pid(pid_);
  request.set_priority(Priority::BACKGROUND_BATCH);

  EnqueueJob job(storage_module_, request, std::move(delegate));

  auto status = job.Cancel(failure_status);
  EXPECT_TRUE(status.ok());
}

}  // namespace
}  // namespace reporting
