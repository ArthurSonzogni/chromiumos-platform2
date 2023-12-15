// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/upload_job.h"

#include <utility>
#include <vector>

#include <base/location.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/bind_post_task.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/test/task_environment.h>
#include <base/thread_annotations.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/dbus/mock_upload_client.h"
#include "missive/health/health_module.h"
#include "missive/health/health_module_delegate_mock.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/resources/resource_manager.h"
#include "missive/util/status_macros.h"
#include "missive/util/test_support_callbacks.h"
#include "missive/util/test_util.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::SizeIs;
using ::testing::WithArgs;

namespace reporting {
namespace {

class TestRecordUploader {
 public:
  TestRecordUploader(
      scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
      std::vector<EncryptedRecord> records,
      scoped_refptr<ResourceManager> memory_resource)
      : sequenced_task_runner_(sequenced_task_runner),
        records_(std::move(records)),
        memory_resource_(memory_resource) {
    DETACH_FROM_SEQUENCE(sequence_checker_);
  }

  ~TestRecordUploader() { DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_); }

  void StartUpload(
      StatusOr<std::unique_ptr<UploaderInterface>> uploader_interface_result) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    EXPECT_OK(uploader_interface_result) << uploader_interface_result.error();
    uploader_interface_ = std::move(uploader_interface_result.value());
    Upload(/*send_next_record=*/true);
  }

  base::WeakPtr<TestRecordUploader> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  void Upload(bool send_next_record) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!send_next_record || records_.empty()) {
      uploader_interface_->Completed(Status::StatusOK());
      uploader_interface_.reset();  // Do not need it anymore.
      return;
    }

    ScopedReservation record_reservation(records_.front().ByteSizeLong(),
                                         memory_resource_);
    auto first_record = std::move(*records_.begin());
    records_.erase(records_.begin());
    uploader_interface_->ProcessRecord(
        std::move(first_record), std::move(record_reservation),
        base::BindPostTaskToCurrentDefault(
            base::BindOnce(&TestRecordUploader::Upload, GetWeakPtr())));
  }

  // To protect |records_| running uploads on sequence.
  const scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  std::vector<EncryptedRecord> records_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<UploaderInterface> uploader_interface_
      GUARDED_BY_CONTEXT(sequence_checker_);

  const scoped_refptr<ResourceManager> memory_resource_;

  base::WeakPtrFactory<TestRecordUploader> weak_ptr_factory_{this};
};

class UploadJobTest : public ::testing::Test {
 protected:
  void SetUp() override {
    health_module_ =
        HealthModule::Create(std::make_unique<HealthModuleDelegateMock>());
    memory_resource_ =
        base::MakeRefCounted<ResourceManager>(4u * 1024LLu * 1024LLu);  // 4 MiB
  }

  void TearDown() override {
    // Let all scheduled actions finish.
    task_environment_.RunUntilIdle();

    EXPECT_THAT(memory_resource_->GetUsed(), Eq(0uL));
  }

  base::test::TaskEnvironment task_environment_;

  scoped_refptr<ResourceManager> memory_resource_;
  scoped_refptr<HealthModule> health_module_;
};

TEST_F(UploadJobTest, UploadsRecords) {
  static constexpr char kTestData[] = "TEST_DATA";
  static constexpr int64_t kSequenceId = 42;
  static constexpr int64_t kGenerationId = 1701;
  static constexpr Priority kPriority = Priority::SLOW_BATCH;

  std::vector<EncryptedRecord> records;
  for (size_t seq_id = 0; seq_id < 10; seq_id++) {
    records.emplace_back();
    EncryptedRecord& encrypted_record = records.back();
    encrypted_record.set_encrypted_wrapped_record(kTestData);

    SequenceInformation* sequence_information =
        encrypted_record.mutable_sequence_information();
    sequence_information->set_sequencing_id(kSequenceId);
    sequence_information->set_generation_id(kGenerationId);
    sequence_information->set_priority(kPriority);
  }

  // Create mock client and a copy of the records to ensure they are passed
  // correctly.
  auto upload_client = base::MakeRefCounted<test::MockUploadClient>();
  const std::vector<EncryptedRecord> expected_records(records);
  EXPECT_CALL(*upload_client, SendEncryptedRecords(_, _, _, _, _, _))
      .WillOnce(WithArgs<0, 5>(Invoke(
          [&expected_records](
              std::vector<EncryptedRecord> records,
              UploadClient::HandleUploadResponseCallback response_callback) {
            ASSERT_THAT(records, SizeIs(expected_records.size()));
            for (size_t i = 0; i < records.size(); i++) {
              EXPECT_THAT(records[i], EqualsProto(expected_records[i]));
            }
            UploadEncryptedRecordResponse upload_response;
            upload_response.mutable_status()->set_code(error::OK);
            std::move(response_callback).Run(std::move(upload_response));
          })));

  const auto test_sequenced_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::BEST_EFFORT});
  std::unique_ptr<TestRecordUploader, base::OnTaskRunnerDeleter>
      record_uploader{
          new TestRecordUploader(test_sequenced_task_runner, std::move(records),
                                 memory_resource_),
          base::OnTaskRunnerDeleter(test_sequenced_task_runner)};

  test::TestEvent<StatusOr<UploadEncryptedRecordResponse>> upload_responded;
  auto job_result = UploadJob::Create(
      upload_client,
      /*need_encryption_key=*/false, health_module_,
      /*remaining_storage_capacity=*/3000U,
      /*new_events_rate=*/300U,
      base::BindPostTask(test_sequenced_task_runner,
                         base::BindOnce(&TestRecordUploader::StartUpload,
                                        record_uploader->GetWeakPtr())),
      upload_responded.cb());
  ASSERT_OK(job_result) << job_result.error();
  Scheduler::Job::SmartPtr<Scheduler::Job> job = std::move(job_result.value());

  test::TestEvent<Status> upload_started;
  job->Start(upload_started.cb());
  const Status status = upload_started.result();
  EXPECT_OK(status) << status;
  // Let everything finish before `record_uploader` destructs.
  const auto upload_result = upload_responded.result();
  EXPECT_OK(upload_result) << upload_result.error();
}
}  // namespace
}  // namespace reporting
