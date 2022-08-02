// Copyright 2022 The Chromium OS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/daemon/missive_daemon.h"

#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <brillo/message_loops/base_message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/dbus/dbus_adaptor.h"
#include "missive/missive/missive_service.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/test_support_callbacks.h"

using ::brillo::dbus_utils::AsyncEventSequencer;

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;

namespace reporting {
namespace {

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

class MockMissive : public MissiveService {
 public:
  MockMissive() = default;

  MOCK_METHOD(void, StartUp, (base::OnceCallback<void(Status)> cb), (override));
  MOCK_METHOD(Status, ShutDown, (), (override));
  MOCK_METHOD(void, OnReady, (), (const override));

  MOCK_METHOD(void,
              AsyncStartUpload,
              (UploaderInterface::UploadReason reason,
               UploaderInterface::UploaderInterfaceResultCb uploader_result_cb),
              (override));
  MOCK_METHOD(void,
              EnqueueRecord,
              (const EnqueueRecordRequest& in_request,
               std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   EnqueueRecordResponse>> out_response),
              (override));
  MOCK_METHOD(void,
              FlushPriority,
              (const FlushPriorityRequest& in_request,
               std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   FlushPriorityResponse>> out_response),
              (override));
  MOCK_METHOD(void,
              ConfirmRecordUpload,
              (const ConfirmRecordUploadRequest& in_request,
               std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   ConfirmRecordUploadResponse>> out_response),
              (override));
  MOCK_METHOD(void,
              UpdateEncryptionKey,
              (const UpdateEncryptionKeyRequest& in_request,
               std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   UpdateEncryptionKeyResponse>> out_response),
              (override));
};

class MissiveDaemonTest : public ::testing::Test {
 public:
  MissiveDaemonTest() = default;

  void SetUp() override {
    auto mock_missive = std::make_unique<StrictMock<MockMissive>>();
    mock_missive_ = mock_missive.get();

    dbus::Bus::Options options;
    mock_bus_ = base::MakeRefCounted<NiceMock<dbus::MockBus>>(options);
    dbus::ObjectPath path(missive::kMissiveServicePath);

    mock_object_proxy_ = base::MakeRefCounted<NiceMock<dbus::MockObjectProxy>>(
        mock_bus_.get(), missive::kMissiveServicePath, path);

    mock_exported_object_ =
        base::MakeRefCounted<StrictMock<dbus::MockExportedObject>>(
            mock_bus_.get(), path);

    ON_CALL(*mock_bus_, GetExportedObject(path))
        .WillByDefault(Return(mock_exported_object_.get()));

    ON_CALL(*mock_bus_, GetDBusTaskRunner())
        .WillByDefault(
            Return(task_environment_.GetMainThreadTaskRunner().get()));

    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .Times(testing::AnyNumber());

    auto missive = std::make_unique<StrictMock<MockMissive>>();
    mock_missive_ = missive.get();
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(*mock_missive_, StartUp(_))
        .WillOnce(Invoke([&waiter](base::OnceCallback<void(Status)> cb) {
          std::move(cb).Run(Status::StatusOK());
          waiter.Signal();
        }));
    missive_daemon_.reset(new DBusAdaptor(mock_bus_, std::move(missive)));
    brillo_loop_.SetAsCurrent();
  }

  void TearDown() override {
    EXPECT_CALL(*mock_missive_, ShutDown())
        .WillOnce(Return(Status::StatusOK()));
    missive_daemon_->Shutdown();
  }

  void WaitForReady() {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(*mock_missive_, OnReady())
        .WillOnce(Invoke(&waiter, &test::TestCallbackWaiter::Signal));
  }

 protected:
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  brillo::BaseMessageLoop brillo_loop_{
      task_environment_.GetMainThreadTaskRunner().get()};

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  StrictMock<MockMissive>* mock_missive_ = nullptr;
  std::unique_ptr<DBusAdaptor> missive_daemon_;
};

TEST_F(MissiveDaemonTest, EnqueueRecordTest) {
  WaitForReady();

  EnqueueRecordRequest request;
  request.mutable_record()->set_data("DATA");
  request.mutable_record()->set_destination(HEARTBEAT_EVENTS);
  request.set_priority(FAST_BATCH);

  EXPECT_CALL(*mock_missive_, EnqueueRecord(EqualsProto(request), _))
      .WillOnce([](const EnqueueRecordRequest& in_request,
                   std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       EnqueueRecordResponse>> out_response) {
        EnqueueRecordResponse response;  // Success
        out_response->Return(response);
      });

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
  test::TestCallbackAutoWaiter waiter;
  response->set_return_callback(base::BindOnce(
      [](test::TestCallbackWaiter* waiter,
         const EnqueueRecordResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(error::OK));
        waiter->Signal();
      },
      &waiter));
  missive_daemon_->EnqueueRecord(std::move(response), request);
}

TEST_F(MissiveDaemonTest, FlushPriorityTest) {
  WaitForReady();

  FlushPriorityRequest request;
  request.set_priority(MANUAL_BATCH);

  EXPECT_CALL(*mock_missive_, FlushPriority(EqualsProto(request), _))
      .WillOnce([](const FlushPriorityRequest& in_request,
                   std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       FlushPriorityResponse>> out_response) {
        FlushPriorityResponse response;  // Success
        out_response->Return(response);
      });

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<FlushPriorityResponse>>();
  test::TestCallbackAutoWaiter waiter;
  response->set_return_callback(base::BindOnce(
      [](test::TestCallbackWaiter* waiter,
         const FlushPriorityResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(error::OK));
        waiter->Signal();
      },
      &waiter));
  missive_daemon_->FlushPriority(std::move(response), request);
}

TEST_F(MissiveDaemonTest, ConfirmRecordUploadTest) {
  WaitForReady();

  ConfirmRecordUploadRequest request;
  request.mutable_sequence_information()->set_sequencing_id(1234L);
  request.mutable_sequence_information()->set_generation_id(9876L);
  request.mutable_sequence_information()->set_priority(IMMEDIATE);
  request.set_force_confirm(true);

  EXPECT_CALL(*mock_missive_, ConfirmRecordUpload(EqualsProto(request), _))
      .WillOnce([](const ConfirmRecordUploadRequest& in_request,
                   std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       ConfirmRecordUploadResponse>> out_response) {
        ConfirmRecordUploadResponse response;  // Success
        out_response->Return(response);
      });

  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      ConfirmRecordUploadResponse>>();
  test::TestCallbackAutoWaiter waiter;
  response->set_return_callback(base::BindOnce(
      [](test::TestCallbackWaiter* waiter,
         const ConfirmRecordUploadResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(error::OK));
        waiter->Signal();
      },
      &waiter));
  missive_daemon_->ConfirmRecordUpload(std::move(response), request);
}

TEST_F(MissiveDaemonTest, UpdateEncryptionKeyTest) {
  WaitForReady();

  UpdateEncryptionKeyRequest request;
  request.mutable_signed_encryption_info()->set_public_asymmetric_key(
      "PUBLIC_KEY");
  request.mutable_signed_encryption_info()->set_public_key_id(555666);
  request.mutable_signed_encryption_info()->set_signature("SIGNATURE");

  EXPECT_CALL(*mock_missive_, UpdateEncryptionKey(EqualsProto(request), _))
      .WillOnce([](const UpdateEncryptionKeyRequest& in_request,
                   std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       UpdateEncryptionKeyResponse>> out_response) {
        UpdateEncryptionKeyResponse response;  // Success
        out_response->Return(response);
      });

  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      UpdateEncryptionKeyResponse>>();
  test::TestCallbackAutoWaiter waiter;
  response->set_return_callback(base::BindOnce(
      [](test::TestCallbackWaiter* waiter,
         const UpdateEncryptionKeyResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(error::OK));
        waiter->Signal();
      },
      &waiter));
  missive_daemon_->UpdateEncryptionKey(std::move(response), request);
}

TEST_F(MissiveDaemonTest, ResponseWithErrorTest) {
  WaitForReady();

  const Status error{error::INTERNAL, "Test generated error"};

  FlushPriorityRequest request;
  request.set_priority(SLOW_BATCH);

  EXPECT_CALL(*mock_missive_, FlushPriority(EqualsProto(request), _))
      .WillOnce([&error](const FlushPriorityRequest& in_request,
                         std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                             FlushPriorityResponse>> out_response) {
        FlushPriorityResponse response;
        error.SaveTo(response.mutable_status());
        out_response->Return(response);
      });

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<FlushPriorityResponse>>();
  test::TestCallbackAutoWaiter waiter;
  response->set_return_callback(base::BindOnce(
      [](test::TestCallbackWaiter* waiter, Status expected_error,
         const FlushPriorityResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(expected_error.error_code()));
        EXPECT_THAT(response.status().error_message(),
                    StrEq(std::string(expected_error.error_message())));
        waiter->Signal();
      },
      &waiter, error));
  missive_daemon_->FlushPriority(std::move(response), request);
}

TEST_F(MissiveDaemonTest, UnavailableTest) {
  FlushPriorityRequest request;
  request.set_priority(IMMEDIATE);

  EXPECT_CALL(*mock_missive_, FlushPriority(EqualsProto(request), _)).Times(0);

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<FlushPriorityResponse>>();
  test::TestCallbackAutoWaiter waiter;
  response->set_return_callback(base::BindOnce(
      [](test::TestCallbackWaiter* waiter,
         const FlushPriorityResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(error::UNAVAILABLE));
        waiter->Signal();
      },
      &waiter));
  missive_daemon_->FlushPriority(std::move(response), request);
}
}  // namespace
}  // namespace reporting
