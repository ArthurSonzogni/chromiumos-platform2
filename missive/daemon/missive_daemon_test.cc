// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <brillo/message_loops/base_message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <featured/feature_library.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/analytics/metrics.h"
#include "missive/analytics/metrics_test_util.h"
#include "missive/dbus/dbus_adaptor.h"
#include "missive/missive/missive_service.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/reporting_errors.h"
#include "missive/util/status.h"
#include "missive/util/test_support_callbacks.h"
#include "missive/util/test_util.h"

using ::brillo::dbus_utils::AsyncEventSequencer;

using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Property;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::WithArg;

namespace reporting {
namespace {

class MockMissive : public MissiveService {
 public:
  MockMissive() = default;

  MOCK_METHOD(void,
              StartUp,
              (scoped_refptr<dbus::Bus> bus,
               feature::PlatformFeaturesInterface* feature_lib,
               base::OnceCallback<void(Status)> cb),
              (override));

  MOCK_METHOD(Status, ShutDown, (), (override));
  MOCK_METHOD(void, OnReady, (), (const override));

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
              UpdateConfigInMissive,
              (const UpdateConfigInMissiveRequest& in_request,
               std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   UpdateConfigInMissiveResponse>> out_response),
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

  void TearDown() override {
    if (missive_daemon_) {
      if (mock_missive_) {
        EXPECT_CALL(*mock_missive_, ShutDown()).Times(1);
        mock_missive_ = nullptr;
      }
      feature::PlatformFeatures::ShutdownForTesting();
      missive_daemon_->Shutdown();
      missive_daemon_.reset();
    }
  }

  void StartUp(
      Status status = Status::StatusOK(),
      base::OnceCallback<void(Status)> failure_cb = base::DoNothing()) {
    ASSERT_FALSE(mock_missive_) << "Can call StartUp only once";

    mock_bus_ =
        base::MakeRefCounted<NiceMock<dbus::MockBus>>(dbus::Bus::Options());
    dbus::ObjectPath path(missive::kMissiveServicePath);

    mock_exported_object_ =
        base::MakeRefCounted<StrictMock<dbus::MockExportedObject>>(
            mock_bus_.get(), path);

    ON_CALL(*mock_bus_, GetExportedObject(path))
        .WillByDefault(Return(mock_exported_object_.get()));

    ON_CALL(*mock_bus_, GetDBusTaskRunner())
        .WillByDefault(
            Return(task_environment_.GetMainThreadTaskRunner().get()));

    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .Times(AnyNumber());

    mock_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        mock_bus_.get(), chromeos::kChromeFeaturesServiceName,
        dbus::ObjectPath(chromeos::kChromeFeaturesServicePath));

    mock_dbus_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        mock_bus_.get(), "org.freedesktop.DBus",
        dbus::ObjectPath("/org/freedesktop/DBus"));

    ON_CALL(*mock_bus_, GetObjectProxy(_, _))
        .WillByDefault(Return(mock_proxy_.get()));

    ON_CALL(*mock_bus_,
            GetObjectProxy("org.freedesktop.DBus",
                           dbus::ObjectPath("/org/freedesktop/DBus")))
        .WillByDefault(Return(mock_dbus_proxy_.get()));

    auto missive = std::make_unique<StrictMock<MockMissive>>();
    mock_missive_ = missive.get();
    EXPECT_CALL(*mock_missive_, StartUp(NotNull(), _, _))
        .WillOnce(WithArg<2>([&status](base::OnceCallback<void(Status)> cb) {
          std::move(cb).Run(status);
        }));

    missive_daemon_ = std::make_unique<DBusAdaptor>(
        mock_bus_, std::move(missive), std::move(failure_cb));
  }

  void WaitForReady() {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(*mock_missive_, OnReady())
        .WillOnce(Invoke(&waiter, &test::TestCallbackWaiter::Signal));
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  analytics::Metrics::TestEnvironment metrics_test_environment_;

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  // Necessary for feature::PlatformFeatures::Initialize in DbusAdaptor.
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  scoped_refptr<dbus::MockObjectProxy> mock_dbus_proxy_;
  StrictMock<MockMissive>* mock_missive_ = nullptr;
  std::unique_ptr<DBusAdaptor> missive_daemon_;
};

TEST_F(MissiveDaemonTest, EnqueueRecordTest) {
  StartUp();
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
  test::TestEvent<const EnqueueRecordResponse&> response_event;
  response->set_return_callback(response_event.cb());

  dbus::MethodCall method_call("org.chromium.Missived", "EnqueueRecord");
  method_call.SetSender(":1.1");

  missive_daemon_->EnqueueRecord(std::move(response), &method_call, request);
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveDaemonTest, EnqueueRecordControlledTest) {
  StartUp();
  WaitForReady();

  EnqueueRecordRequest request;
  request.mutable_record()->set_data("DATA");
  request.mutable_record()->set_destination(CROS_SECURITY_PROCESS);
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
  test::TestEvent<const EnqueueRecordResponse&> response_event;
  response->set_return_callback(response_event.cb());

  dbus::MethodCall method_call("org.chromium.Missived", "EnqueueRecord");
  method_call.SetSender(":1.1");

  EXPECT_CALL(*mock_dbus_proxy_, CallMethod(_, _, _))
      .WillOnce(
          WithArg<2>(Invoke([](dbus::ObjectProxy::ResponseCallback callback) {
            auto dbus_response = dbus::Response::CreateEmpty();
            dbus::MessageWriter writer(dbus_response.get());
            writer.AppendUint32(0);  // Authorized root UID
            std::move(callback).Run(dbus_response.get());
          })));

  missive_daemon_->EnqueueRecord(std::move(response), &method_call, request);
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveDaemonTest, EnqueueRecordControlledUnauthorizedTest) {
  StartUp();
  WaitForReady();

  EnqueueRecordRequest request;
  request.mutable_record()->set_data("DATA");
  request.mutable_record()->set_destination(CROS_SECURITY_PROCESS);
  request.set_priority(FAST_BATCH);

  EXPECT_CALL(*mock_missive_, EnqueueRecord(_, _)).Times(0);
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(
          kUmaPermissionDeniedErrorReason,
          static_cast<int>(
              PermissionDeniedErrorReason::SECURE_DESTINATION_FORBIDDEN),
          static_cast<int>(PermissionDeniedErrorReason::MAX_VALUE)))
      .Times(1);

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
  test::TestEvent<const EnqueueRecordResponse&> response_event;
  response->set_return_callback(response_event.cb());

  dbus::MethodCall method_call("org.chromium.Missived", "EnqueueRecord");
  method_call.SetSender(":1.1");

  EXPECT_CALL(*mock_dbus_proxy_, CallMethod(_, _, _))
      .WillOnce(
          WithArg<2>(Invoke([](dbus::ObjectProxy::ResponseCallback callback) {
            auto dbus_response = dbus::Response::CreateEmpty();
            dbus::MessageWriter writer(dbus_response.get());
            writer.AppendUint32(1000);  // Unauthorized UID
            std::move(callback).Run(dbus_response.get());
          })));

  missive_daemon_->EnqueueRecord(std::move(response), &method_call, request);
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::PERMISSION_DENIED));
}

TEST_F(MissiveDaemonTest, EnqueueRecordCacheEvictionOnDisconnectTest) {
  StartUp();
  WaitForReady();

  EnqueueRecordRequest request;
  request.mutable_record()->set_destination(CROS_SECURITY_PROCESS);
  request.set_priority(FAST_BATCH);

  // Expect CallMethod to fetch UID twice because the cache is cleared between
  // calls.
  EXPECT_CALL(*mock_dbus_proxy_, CallMethod(_, _, _))
      .Times(2)
      .WillRepeatedly(
          WithArg<2>(Invoke([](dbus::ObjectProxy::ResponseCallback callback) {
            auto dbus_response = dbus::Response::CreateEmpty();
            dbus::MessageWriter writer(dbus_response.get());
            writer.AppendUint32(0);  // Authorized root UID
            std::move(callback).Run(dbus_response.get());
          })));

  EXPECT_CALL(*mock_missive_, EnqueueRecord(_, _))
      .Times(2)
      .WillRepeatedly([](const EnqueueRecordRequest& in_request,
                         std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                             EnqueueRecordResponse>> out_response) {
        EnqueueRecordResponse response;
        out_response->Return(response);
      });

  dbus::MethodCall method_call("org.chromium.Missived", "EnqueueRecord");
  method_call.SetSender(":1.1");

  {
    auto response = std::make_unique<
        brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
    missive_daemon_->EnqueueRecord(std::move(response), &method_call, request);
  }

  // Deliver NameOwnerChanged signal where new owner is empty (disconnect)
  dbus::Signal name_owner_changed_signal("org.freedesktop.DBus",
                                         "NameOwnerChanged");
  dbus::MessageWriter writer(&name_owner_changed_signal);
  writer.AppendString(":1.1");  // Unique connection name
  writer.AppendString(":1.1");  // Old owner
  writer.AppendString("");      // New owner (empty means disconnected)

  missive_daemon_->OnNameOwnerChanged(&name_owner_changed_signal);

  // Second call: should trigger D-Bus call again due to cache eviction
  {
    auto response = std::make_unique<
        brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
    missive_daemon_->EnqueueRecord(std::move(response), &method_call, request);
  }
}

TEST_F(MissiveDaemonTest, EnqueueRecordCacheHitTest) {
  StartUp();
  WaitForReady();

  EnqueueRecordRequest request;
  request.mutable_record()->set_destination(CROS_SECURITY_PROCESS);
  request.set_priority(FAST_BATCH);

  // First call: expect CallMethod to fetch UID
  EXPECT_CALL(*mock_dbus_proxy_, CallMethod(_, _, _))
      .WillOnce(
          WithArg<2>(Invoke([](dbus::ObjectProxy::ResponseCallback callback) {
            auto dbus_response = dbus::Response::CreateEmpty();
            dbus::MessageWriter writer(dbus_response.get());
            writer.AppendUint32(0);  // Authorized root UID
            std::move(callback).Run(dbus_response.get());
          })));

  EXPECT_CALL(*mock_missive_, EnqueueRecord(_, _))
      .Times(2)
      .WillRepeatedly([](const EnqueueRecordRequest& in_request,
                         std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                             EnqueueRecordResponse>> out_response) {
        EnqueueRecordResponse response;
        out_response->Return(response);
      });

  dbus::MethodCall method_call("org.chromium.Missived", "EnqueueRecord");
  method_call.SetSender(":1.1");

  {
    auto response = std::make_unique<
        brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
    missive_daemon_->EnqueueRecord(std::move(response), &method_call, request);
  }

  // Second call: CallMethod should NOT be called again (cache hit)
  {
    auto response = std::make_unique<
        brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
    missive_daemon_->EnqueueRecord(std::move(response), &method_call, request);
  }
}

TEST_F(MissiveDaemonTest, EnqueueRecordOrderingQueueingTest) {
  StartUp();
  WaitForReady();

  EnqueueRecordRequest request1;
  request1.mutable_record()->set_data("DATA1");
  request1.mutable_record()->set_destination(CROS_SECURITY_PROCESS);
  request1.set_priority(FAST_BATCH);

  EnqueueRecordRequest request2;
  request2.mutable_record()->set_data("DATA2");
  request2.mutable_record()->set_destination(CROS_SECURITY_PROCESS);
  request2.set_priority(FAST_BATCH);

  // We expect EnqueueRecord to be called on mock_missive_ for both in order.
  testing::Sequence s;
  EXPECT_CALL(*mock_missive_, EnqueueRecord(EqualsProto(request1), _))
      .InSequence(s)
      .WillOnce([](const EnqueueRecordRequest& in_request,
                   std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       EnqueueRecordResponse>> out_response) {
        EnqueueRecordResponse response;
        out_response->Return(response);
      });
  EXPECT_CALL(*mock_missive_, EnqueueRecord(EqualsProto(request2), _))
      .InSequence(s)
      .WillOnce([](const EnqueueRecordRequest& in_request,
                   std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       EnqueueRecordResponse>> out_response) {
        EnqueueRecordResponse response;
        out_response->Return(response);
      });

  dbus::ObjectProxy::ResponseCallback dbus_callback;
  // D-Bus CallMethod should be called exactly once for the sender.
  EXPECT_CALL(*mock_dbus_proxy_, CallMethod(_, _, _))
      .WillOnce(WithArg<2>(Invoke(
          [&dbus_callback](dbus::ObjectProxy::ResponseCallback callback) {
            dbus_callback = std::move(callback);
          })));

  auto response1 = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
  test::TestEvent<const EnqueueRecordResponse&> response_event1;
  response1->set_return_callback(response_event1.cb());

  dbus::MethodCall method_call("org.chromium.Missived", "EnqueueRecord");
  method_call.SetSender(":1.1");

  missive_daemon_->EnqueueRecord(std::move(response1), &method_call, request1);

  // Second message comes in from same sender before D-Bus responds.
  auto response2 = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
  test::TestEvent<const EnqueueRecordResponse&> response_event2;
  response2->set_return_callback(response_event2.cb());

  missive_daemon_->EnqueueRecord(std::move(response2), &method_call, request2);

  // Now trigger the D-Bus response callback.
  auto dbus_response = dbus::Response::CreateEmpty();
  dbus::MessageWriter writer(dbus_response.get());
  writer.AppendUint32(0);  // Authorized root UID
  std::move(dbus_callback).Run(dbus_response.get());

  const auto& response_result1 = response_event1.ref_result();
  EXPECT_THAT(response_result1.status().code(), Eq(error::OK));

  const auto& response_result2 = response_event2.ref_result();
  EXPECT_THAT(response_result2.status().code(), Eq(error::OK));
}

TEST_F(MissiveDaemonTest, EnqueueRecordLookupFailureTest) {
  StartUp();
  WaitForReady();

  EnqueueRecordRequest request;
  request.mutable_record()->set_data("DATA");
  request.mutable_record()->set_destination(CROS_SECURITY_PROCESS);
  request.set_priority(FAST_BATCH);

  EXPECT_CALL(*mock_missive_, EnqueueRecord(_, _)).Times(0);
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(kUmaUnavailableErrorReason,
                    static_cast<int>(
                        UnavailableErrorReason::SENDER_UNIX_USER_LOOKUP_FAILED),
                    static_cast<int>(UnavailableErrorReason::MAX_VALUE)))
      .Times(1);

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<EnqueueRecordResponse>>();
  test::TestEvent<const EnqueueRecordResponse&> response_event;
  response->set_return_callback(response_event.cb());

  dbus::MethodCall method_call("org.chromium.Missived", "EnqueueRecord");
  method_call.SetSender(":1.1");

  // CallMethod fails (returns nullptr response)
  EXPECT_CALL(*mock_dbus_proxy_, CallMethod(_, _, _))
      .WillOnce(
          WithArg<2>(Invoke([](dbus::ObjectProxy::ResponseCallback callback) {
            std::move(callback).Run(nullptr);
          })));

  missive_daemon_->EnqueueRecord(std::move(response), &method_call, request);
  const auto& response_result = response_event.ref_result();
  // Expect UNAVAILABLE (retryable transient error) instead of
  // PERMISSION_DENIED.
  EXPECT_THAT(response_result.status().code(), Eq(error::UNAVAILABLE));
}

TEST_F(MissiveDaemonTest, FlushPriorityTest) {
  StartUp();
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
  test::TestEvent<const FlushPriorityResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_daemon_->FlushPriority(std::move(response), request);
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveDaemonTest, ConfirmRecordUploadTest) {
  StartUp();
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
  test::TestEvent<const ConfirmRecordUploadResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_daemon_->ConfirmRecordUpload(std::move(response), request);
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveDaemonTest, UpdateEncryptionKeyTest) {
  StartUp();
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
  test::TestEvent<const UpdateEncryptionKeyResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_daemon_->UpdateEncryptionKey(std::move(response), request);
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status().code(), Eq(error::OK));
}

TEST_F(MissiveDaemonTest, ResponseWithErrorTest) {
  StartUp();
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
  test::TestEvent<const FlushPriorityResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_daemon_->FlushPriority(std::move(response), request);
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status(),
              AllOf(Property(&StatusProto::code, Eq(error.error_code())),
                    Property(&StatusProto::error_message,
                             StrEq(std::string(error.error_message())))));
}

TEST_F(MissiveDaemonTest, UnavailableTest) {
  const Status failure_status =
      Status(error::UNAVAILABLE, "Test did not start daemon");
  test::TestEvent<Status> failure_event;
  StartUp(failure_status, failure_event.cb());
  const auto result = failure_event.result();
  ASSERT_THAT(
      result,
      AllOf(Property(&Status::error_code, Eq(error::UNAVAILABLE)),
            Property(&Status::error_message,
                     StrEq(std::string(failure_status.error_message())))))
      << result;

  FlushPriorityRequest request;
  request.set_priority(IMMEDIATE);

  EXPECT_CALL(*mock_missive_, FlushPriority(EqualsProto(request), _)).Times(0);

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<FlushPriorityResponse>>();
  test::TestEvent<const FlushPriorityResponse&> response_event;
  response->set_return_callback(response_event.cb());
  missive_daemon_->FlushPriority(std::move(response), request);
  const auto& response_result = response_event.ref_result();
  EXPECT_THAT(response_result.status(),
              AllOf(Property(&StatusProto::code, Eq(error::UNAVAILABLE)),
                    Property(&StatusProto::error_message,
                             StrEq("The daemon is still starting."))));
}
}  // namespace
}  // namespace reporting
