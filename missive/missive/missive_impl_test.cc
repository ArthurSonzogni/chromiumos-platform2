// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive/missive_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/guid.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/analytics/resource_collector_cpu.h"
#include "missive/analytics/resource_collector_memory.h"
#include "missive/analytics/resource_collector_storage.h"
#include "missive/dbus/mock_upload_client.h"
#include "missive/storage/storage_module.h"
#include "missive/util/test_support_callbacks.h"
#include "missive/util/test_util.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::WithArg;

namespace reporting {
namespace {

class MockStorageModule : public StorageModule {
 public:
  // As opposed to the production |StorageModule|, test module does not need to
  // call factory method - it is created directly by constructor.
  MockStorageModule() = default;

  MOCK_METHOD(void,
              AddRecord,
              (Priority priority, Record record, EnqueueCallback callback),
              (override));

  MOCK_METHOD(void,
              Flush,
              (Priority priority, FlushCallback callback),
              (override));

  MOCK_METHOD(void,
              ReportSuccess,
              (SequenceInformation sequence_information, bool force),
              (override));

  MOCK_METHOD(void,
              UpdateEncryptionKey,
              (SignedEncryptionInfo signed_encryption_key),
              (override));

  MOCK_METHOD(base::StringPiece, GetPipelineId, (), (const override));
};

class MissiveImplTest : public ::testing::Test {
 public:
  MissiveImplTest() = default;

 protected:
  void SetUp() override {
    missive_ = std::make_unique<MissiveImpl>(
        std::make_unique<MissiveArgs>("", "", "", ""),
        base::BindOnce(
            [](MissiveImplTest* self, scoped_refptr<dbus::Bus> bus,
               base::OnceCallback<void(StatusOr<scoped_refptr<UploadClient>>)>
                   callback) {
              self->upload_client_ =
                  base::MakeRefCounted<test::MockUploadClient>();
              std::move(callback).Run(self->upload_client_);
            },
            base::Unretained(this)),
        base::BindOnce(
            [](MissiveImplTest* self, MissiveImpl* missive,
               StorageOptions storage_options,
               base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
                   callback) {
              self->storage_module_ = base::MakeRefCounted<MockStorageModule>();
              std::move(callback).Run(self->storage_module_);
            },
            base::Unretained(this)));

    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    test::TestEvent<Status> started;
    missive_->StartUp(base::MakeRefCounted<NiceMock<dbus::MockBus>>(options),
                      started.cb());
    ASSERT_OK(started.result());
  }

  void TearDown() override {
    if (missive_) {
      EXPECT_OK(missive_->ShutDown());
    }
    // Let everything ongoing to finish.
    task_environment_.RunUntilIdle();
  }

  base::test::TaskEnvironment task_environment_;

  scoped_refptr<UploadClient> upload_client_;
  scoped_refptr<MockStorageModule> storage_module_;
  std::unique_ptr<MissiveImpl> missive_;
};

TEST_F(MissiveImplTest, GetPipelineId) {
  ASSERT_TRUE(storage_module_);
  EXPECT_CALL(*storage_module_, GetPipelineId())
      .WillOnce(Return(base::GenerateGUID()));
  EXPECT_TRUE(base::IsValidGUID(storage_module_->GetPipelineId()));
}

TEST_F(MissiveImplTest, AsyncStartUploadTest) {
  test::TestEvent<StatusOr<std::unique_ptr<UploaderInterface>>> uploader_event;
  MissiveImpl::AsyncStartUpload(
      missive_->GetWeakPtr(), UploaderInterface::UploadReason::IMMEDIATE_FLUSH,
      uploader_event.cb());
  auto response_result = uploader_event.result();
  EXPECT_OK(response_result) << response_result.status();
  response_result.ValueOrDie()->Completed(
      Status(error::INTERNAL, "Failing for tests"));
}

TEST_F(MissiveImplTest, AsyncNoStartUploadTest) {
  test::TestEvent<StatusOr<std::unique_ptr<UploaderInterface>>> uploader_event;
  auto weak_ptr = missive_->GetWeakPtr();
  missive_.reset();
  MissiveImpl::AsyncStartUpload(
      weak_ptr, UploaderInterface::UploadReason::IMMEDIATE_FLUSH,
      uploader_event.cb());
  auto response_result = uploader_event.result();
  EXPECT_THAT(response_result.status().code(), Eq(error::UNAVAILABLE))
      << response_result.status();
}

TEST_F(MissiveImplTest, EnqueueRecordTest) {
  EnqueueRecordRequest request;
  request.mutable_record()->set_data("DATA");
  request.mutable_record()->set_destination(HEARTBEAT_EVENTS);
  request.set_priority(FAST_BATCH);

  EXPECT_CALL(*storage_module_, AddRecord(Eq(request.priority()),
                                          EqualsProto(request.record()), _))
      .WillOnce(
          WithArg<2>([](StorageModuleInterface::EnqueueCallback callback) {
            std::move(callback).Run(Status::StatusOK());
          }));

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
  test::TestEvent<const EnqueueRecordResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_->EnqueueRecord(request, std::move(response));
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveImplTest, FlushPriorityTest) {
  FlushPriorityRequest request;
  request.set_priority(MANUAL_BATCH);

  EXPECT_CALL(*storage_module_, Flush(Eq(request.priority()), _))
      .WillOnce(WithArg<1>([](StorageModuleInterface::FlushCallback callback) {
        std::move(callback).Run(Status::StatusOK());
      }));

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<FlushPriorityResponse>>();
  test::TestEvent<const FlushPriorityResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_->FlushPriority(request, std::move(response));
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveImplTest, ConfirmRecordUploadTest) {
  ConfirmRecordUploadRequest request;
  request.mutable_sequence_information()->set_sequencing_id(1234L);
  request.mutable_sequence_information()->set_generation_id(9876L);
  request.mutable_sequence_information()->set_priority(IMMEDIATE);
  request.set_force_confirm(true);

  EXPECT_CALL(*storage_module_,
              ReportSuccess(EqualsProto(request.sequence_information()),
                            Eq(request.force_confirm())))
      .Times(1);

  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      ConfirmRecordUploadResponse>>();
  test::TestEvent<const ConfirmRecordUploadResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_->ConfirmRecordUpload(request, std::move(response));
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveImplTest, UpdateEncryptionKeyTest) {
  UpdateEncryptionKeyRequest request;
  request.mutable_signed_encryption_info()->set_public_asymmetric_key(
      "PUBLIC_KEY");
  request.mutable_signed_encryption_info()->set_public_key_id(555666);
  request.mutable_signed_encryption_info()->set_signature("SIGNATURE");

  EXPECT_CALL(
      *storage_module_,
      UpdateEncryptionKey(EqualsProto(request.signed_encryption_info())))
      .Times(1);

  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      UpdateEncryptionKeyResponse>>();
  test::TestEvent<const UpdateEncryptionKeyResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_->UpdateEncryptionKey(request, std::move(response));
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveImplTest, ResponseWithErrorTest) {
  const Status error{error::INTERNAL, "Test generated error"};

  FlushPriorityRequest request;
  request.set_priority(SLOW_BATCH);

  EXPECT_CALL(*storage_module_, Flush(Eq(request.priority()), _))
      .WillOnce(
          WithArg<1>([&error](StorageModuleInterface::FlushCallback callback) {
            std::move(callback).Run(error);
          }));

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<FlushPriorityResponse>>();
  test::TestEvent<const FlushPriorityResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_->FlushPriority(request, std::move(response));
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error.error_code()));
  EXPECT_THAT(response_result.status().error_message(),
              StrEq(std::string(error.error_message())));
}
}  // namespace
}  // namespace reporting
