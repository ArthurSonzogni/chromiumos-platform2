// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/upload_job.h"

#include <string>
#include <utility>
#include <vector>

#include <base/run_loop.h>
#include <base/memory/scoped_refptr.h>
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
      : records_(std::move(records)) {}

  void StartUpload(
      StatusOr<std::unique_ptr<UploaderInterface>> uploader_interface) {
    EXPECT_TRUE(uploader_interface.ok());
    uploader_interface_ = std::move(uploader_interface.ValueOrDie());
    Upload(true);
  }

 private:
  void Upload(bool send_next_record) {
    if (!send_next_record || records_.empty()) {
      uploader_interface_->Completed(Status::StatusOK());
      return;
    }
    uploader_interface_->ProcessRecord(
        records_.front(),
        base::BindOnce(&TestRecordUploader::Upload, base::Unretained(this)));
    records_.erase(records_.begin());
  }

  std::vector<EncryptedRecord> records_;
  std::unique_ptr<UploaderInterface> uploader_interface_;
};

class UploadJobTest : public ::testing::Test {
 protected:
  void SetUp() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = scoped_refptr<dbus::MockBus>(new dbus::MockBus(options));

    mock_chrome_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), chromeos::kChromeReportingServiceName,
        dbus::ObjectPath(chromeos::kChromeReportingServicePath));

    upload_client_ = UploadClientProducer::CreateForTests(
        mock_bus_, mock_chrome_proxy_.get());
  }

  base::test::TaskEnvironment task_environment_;

  scoped_refptr<dbus::Bus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_chrome_proxy_;
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

    SequencingInformation* sequencing_information =
        encrypted_record.mutable_sequencing_information();
    sequencing_information->set_sequencing_id(seq_id);
    sequencing_information->set_generation_id(kGenerationId);
    sequencing_information->set_priority(kPriority);
  }

  std::unique_ptr<dbus::Response> dbus_response = dbus::Response::CreateEmpty();
  dbus::Response* dbus_response_ptr = dbus_response.get();
  // Create a copy of the records to ensure they are passed correctly.
  std::vector<EncryptedRecord> expected_records(records);
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
  ASSERT_TRUE(job_result.ok());
  std::unique_ptr<UploadJob> job = std::move(job_result.ValueOrDie());

  test::TestCallbackWaiter waiter;
  waiter.Attach();
  job->Start(base::BindOnce(
      [](test::TestCallbackWaiter* waiter, Status status) {
        EXPECT_TRUE(status.ok());
        waiter->Signal();
      },
      &waiter));
  waiter.Wait();
}

}  // namespace
}  // namespace reporting
