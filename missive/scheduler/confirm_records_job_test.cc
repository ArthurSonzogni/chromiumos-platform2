// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/confirm_records_job.h"

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

#include "missive/compression/test_compression_module.h"
#include "missive/encryption/test_encryption_module.h"
#include "missive/health/health_module.h"
#include "missive/health/health_module_delegate_mock.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_module.h"
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

class MockStorageModule : public StorageModule {
 public:
  static scoped_refptr<MockStorageModule> Create() {
    return base::WrapRefCounted(new MockStorageModule());
  }

  MOCK_METHOD(void,
              ReportSuccess,
              (SequenceInformation sequence_information,
               bool force,
               base::OnceCallback<void(Status)> done_cb),
              (override));

 private:
  class TestUploaderInterface : public UploaderInterface {
   public:
    TestUploaderInterface() = default;
    // Factory method.
    static void AsyncProvideUploader(
        UploaderInterface::UploadReason reason,
        UploaderInterface::InformAboutCachedUploadsCb inform_cb,
        UploaderInterfaceResultCb start_uploader_cb) {
      std::move(start_uploader_cb)
          .Run(std::make_unique<TestUploaderInterface>());
    }

    MOCK_METHOD(void,
                ProcessRecord,
                (EncryptedRecord encrypted_record,
                 ScopedReservation scoped_reservation,
                 base::OnceCallback<void(bool)> processed_cb),
                (override));

    MOCK_METHOD(void,
                ProcessGap,
                (SequenceInformation start,
                 uint64_t count,
                 base::OnceCallback<void(bool)> processed_cb),
                (override));

    MOCK_METHOD(void, Completed, (Status final_status), (override));
  };

  MockStorageModule()
      : StorageModule(Settings{
            .options = StorageOptions(),
            .legacy_storage_enabled = "",
            .queues_container =
                QueuesContainer::Create(/*storage_degradation_enabled=*/false),
            .encryption_module =
                base::MakeRefCounted<test::TestEncryptionModule>(
                    /*is_enabled=*/false),
            .compression_module =
                base::MakeRefCounted<test::TestCompressionModule>(),
            .health_module = HealthModule::Create(
                std::make_unique<HealthModuleDelegateMock>()),
            .server_configuration_controller =
                ServerConfigurationController::Create(/*is_enabled=*/false),
            .signature_verification_dev_flag =
                base::MakeRefCounted<SignatureVerificationDevFlag>(
                    /*is_enabled=*/false),
            .async_start_upload_cb = base::BindRepeating(
                TestUploaderInterface::AsyncProvideUploader)}) {}
};

class ConfirmRecordsJobTest : public ::testing::Test {
 public:
  ConfirmRecordsJobTest() = default;

 protected:
  void SetUp() override {
    response_ = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
        ConfirmRecordUploadResponse>>();

    seq_info_.set_priority(Priority::FAST_BATCH);
    seq_info_.set_generation_id(12345L);
    seq_info_.set_sequencing_id(9876L);
    force_ = true;

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
      brillo::dbus_utils::MockDBusMethodResponse<ConfirmRecordUploadResponse>>
      response_;
  SequenceInformation seq_info_;
  bool force_;

  scoped_refptr<MockStorageModule> storage_module_;

  scoped_refptr<HealthModule> health_module_;
};

TEST_F(ConfirmRecordsJobTest, CompletesSuccessfully) {
  response_->set_return_callback(
      base::BindOnce([](const ConfirmRecordUploadResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(error::OK));
      }));
  auto delegate =
      std::make_unique<ConfirmRecordsJob::ConfirmRecordsResponseDelegate>(
          health_module_, std::move(response_));

  ConfirmRecordUploadRequest request;
  *request.mutable_sequence_information() = seq_info_;
  request.set_force_confirm(force_);

  EXPECT_CALL(*storage_module_,
              ReportSuccess(EqualsProto(seq_info_), Eq(force_), _))
      .WillOnce(WithArgs<2>(Invoke([](base::OnceCallback<void(Status)> cb) {
        std::move(cb).Run(Status::StatusOK());
      })));

  auto job = ConfirmRecordsJob::Create(storage_module_, health_module_, request,
                                       std::move(delegate));

  test::TestEvent<Status> enqueued;
  job->Start(enqueued.cb());
  const Status status = enqueued.result();
  EXPECT_OK(status) << status;
}

TEST_F(ConfirmRecordsJobTest, CancelsSuccessfully) {
  Status failure_status(error::INTERNAL, "Failing for tests");
  response_->set_return_callback(base::BindOnce(
      [](Status failure_status, const ConfirmRecordUploadResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(failure_status.error_code()));
        EXPECT_THAT(response.status().error_message(),
                    StrEq(std::string(failure_status.error_message())));
      },
      failure_status));
  auto delegate =
      std::make_unique<ConfirmRecordsJob::ConfirmRecordsResponseDelegate>(
          health_module_, std::move(response_));

  ConfirmRecordUploadRequest request;
  *request.mutable_sequence_information() = seq_info_;
  request.set_force_confirm(force_);

  EXPECT_CALL(*storage_module_, ReportSuccess(_, _, _)).Times(0);

  auto job = ConfirmRecordsJob::Create(storage_module_, health_module_, request,
                                       std::move(delegate));

  job->Cancel(failure_status);
}

}  // namespace
}  // namespace reporting
