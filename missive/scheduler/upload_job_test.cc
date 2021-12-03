// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/upload_job.h"

#include <string>
#include <utility>
#include <vector>

#include <base/run_loop.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/util/test_support_callbacks.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::WithArgs;

namespace reporting {
namespace {

class UploadClientProducer : public UploadClient {
 public:
  static scoped_refptr<UploadClient> CreateForTests(
      scoped_refptr<dbus::Bus> bus, dbus::ObjectProxy* chrome_proxy) {
    return UploadClient::Create(bus, chrome_proxy);
  }
};

class TestRecordUploader {
 public:
  explicit TestRecordUploader(std::vector<EncryptedRecord> records)
      : records_(std::move(records)),
        sequenced_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
            {base::TaskPriority::BEST_EFFORT})) {
    DETACH_FROM_SEQUENCE(sequence_checker_);
  }

  void StartUpload(
      StatusOr<std::unique_ptr<UploaderInterface>> uploader_interface) {
    EXPECT_TRUE(uploader_interface.ok());
    uploader_interface_ = std::move(uploader_interface.ValueOrDie());
    PostNextUpload(/*next=*/true);
  }

 private:
  void Upload(bool send_next_record) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!send_next_record || records_.empty()) {
      uploader_interface_->Completed(Status::StatusOK());
      uploader_interface_.reset();  // Do not need it anymore.
      return;
    }
    uploader_interface_->ProcessRecord(
        records_.front(), base::BindOnce(&TestRecordUploader::PostNextUpload,
                                         base::Unretained(this)));
    records_.erase(records_.begin());
  }

  void PostNextUpload(bool next) {
    sequenced_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&TestRecordUploader::Upload,
                                  base::Unretained(this), next));
  }

  std::vector<EncryptedRecord> records_;
  std::unique_ptr<UploaderInterface> uploader_interface_;

  // To protect |records_| running uploads on sequence.
  SEQUENCE_CHECKER(sequence_checker_);
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
};

class UploadJobTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dbus_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});

    test::TestEvent<scoped_refptr<NiceMock<dbus::MockBus>>> dbus_waiter;
    dbus_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&UploadJobTest::CreateMockDBus, dbus_waiter.cb()));
    mock_bus_ = dbus_waiter.result();

    EXPECT_CALL(*mock_bus_, GetDBusTaskRunner())
        .WillRepeatedly(Return(dbus_task_runner_.get()));

    EXPECT_CALL(*mock_bus_, GetOriginTaskRunner())
        .WillRepeatedly(Return(dbus_task_runner_.get()));

    // We actually want AssertOnOriginThread and AssertOnDBusThread to work
    // properly (actually assert they are on dbus_thread_). If the unit tests
    // end up creating calls on the wrong thread, the unit test will just hang
    // anyways, and it's easier to debug if we make the program crash at that
    // point. Since these are ON_CALLs, VerifyAndClearMockExpectations doesn't
    // clear them.
    ON_CALL(*mock_bus_, AssertOnOriginThread())
        .WillByDefault(Invoke(this, &UploadJobTest::AssertOnDBusThread));
    ON_CALL(*mock_bus_, AssertOnDBusThread())
        .WillByDefault(Invoke(this, &UploadJobTest::AssertOnDBusThread));

    mock_chrome_proxy_ =
        base::WrapRefCounted(new NiceMock<dbus::MockObjectProxy>(
            mock_bus_.get(), chromeos::kChromeReportingServiceName,
            dbus::ObjectPath(chromeos::kChromeReportingServicePath)));

    upload_client_ = UploadClientProducer::CreateForTests(
        mock_bus_, mock_chrome_proxy_.get());

    upload_client_->SetAvailabilityForTest(/*is_available=*/true);
  }

  void TearDown() override {
    // Let everything ongoing to finish.
    task_environment_.RunUntilIdle();
  }

  static void CreateMockDBus(
      base::OnceCallback<void(scoped_refptr<NiceMock<dbus::MockBus>>)>
          ready_cb) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    std::move(ready_cb).Run(base::WrapRefCounted<NiceMock<dbus::MockBus>>(
        new NiceMock<dbus::MockBus>(options)));
  }

  void AssertOnDBusThread() {
    ASSERT_TRUE(dbus_task_runner_->RunsTasksInCurrentSequence());
  }

  base::test::TaskEnvironment task_environment_;

  scoped_refptr<base::SequencedTaskRunner> dbus_task_runner_;
  scoped_refptr<NiceMock<dbus::MockBus>> mock_bus_;
  scoped_refptr<NiceMock<dbus::MockObjectProxy>> mock_chrome_proxy_;
  scoped_refptr<UploadClient> upload_client_;
};

TEST_F(UploadJobTest, UploadsRecords) {
  const constexpr char kTestData[] = "TEST_DATA";
  const int64_t kGenerationId = 1701;
  const Priority kPriority = Priority::SLOW_BATCH;

  std::vector<EncryptedRecord> records;
  for (size_t seq_id = 0; seq_id < 10; seq_id++) {
    records.emplace_back();
    EncryptedRecord& encrypted_record = records.back();
    encrypted_record.set_encrypted_wrapped_record(kTestData);

    SequenceInformation* sequence_information =
        encrypted_record.mutable_sequence_information();
    sequence_information->set_sequencing_id(seq_id);
    sequence_information->set_generation_id(kGenerationId);
    sequence_information->set_priority(kPriority);
  }

  std::unique_ptr<dbus::Response> dbus_response = dbus::Response::CreateEmpty();
  dbus::Response* dbus_response_ptr = dbus_response.get();
  // Create a copy of the records to ensure they are passed correctly.
  const std::vector<EncryptedRecord> expected_records(records);
  EXPECT_CALL(*mock_chrome_proxy_, DoCallMethod(_, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke(
          [&expected_records, dbus_response_ptr](
              dbus::MethodCall* call,
              base::OnceCallback<void(dbus::Response * response)>* response) {
            ASSERT_EQ(call->GetInterface(),
                      chromeos::kChromeReportingServiceInterface);
            ASSERT_EQ(
                call->GetMember(),
                chromeos::kChromeReportingServiceUploadEncryptedRecordMethod);

            // Read the request.
            dbus::MessageReader reader(call);
            UploadEncryptedRecordRequest request;
            ASSERT_TRUE(reader.PopArrayOfBytesAsProto(&request));

            ASSERT_EQ(request.encrypted_record_size(), expected_records.size());

            for (size_t i = 0; i < request.encrypted_record_size(); i++) {
              std::string expected_serialized;
              expected_records[i].SerializeToString(&expected_serialized);

              std::string requested_serialized;
              request.encrypted_record(i).SerializeToString(
                  &requested_serialized);
              EXPECT_STREQ(requested_serialized.c_str(),
                           expected_serialized.c_str());
            }

            UploadEncryptedRecordResponse upload_response;
            upload_response.mutable_status()->set_code(error::OK);

            ASSERT_TRUE(dbus::MessageWriter(dbus_response_ptr)
                            .AppendProtoAsArrayOfBytes(upload_response));
            std::move(*response).Run(dbus_response_ptr);
          })));

  TestRecordUploader record_uploader(std::move(records));

  auto job_result =
      UploadJob::Create(upload_client_, false,
                        base::BindOnce(&TestRecordUploader::StartUpload,
                                       base::Unretained(&record_uploader)));
  ASSERT_TRUE(job_result.ok()) << job_result.status();
  Scheduler::Job::SmartPtr<Scheduler::Job> job =
      std::move(job_result.ValueOrDie());

  test::TestEvent<Status> uploaded;
  job->Start(uploaded.cb());
  const Status status = uploaded.result();
  EXPECT_OK(status) << status;
  // Let everything finish before record_uploader destructs.
  task_environment_.RunUntilIdle();
}

}  // namespace
}  // namespace reporting
