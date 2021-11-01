// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/dbus/upload_client.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/memory/scoped_refptr.h>
#include <base/sequenced_task_runner.h>
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
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrEq;
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

class UploadClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dbus_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::TaskPriority::BEST_EFFORT, base::MayBlock()});

    test::TestEvent<scoped_refptr<dbus::MockBus>> dbus_waiter;
    dbus_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&UploadClientTest::CreateMockDBus, dbus_waiter.cb()));
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
        .WillByDefault(Invoke(this, &UploadClientTest::AssertOnDBusThread));
    ON_CALL(*mock_bus_, AssertOnDBusThread())
        .WillByDefault(Invoke(this, &UploadClientTest::AssertOnDBusThread));

    mock_chrome_proxy_ = base::WrapRefCounted(new dbus::MockObjectProxy(
        mock_bus_.get(), chromeos::kChromeReportingServiceName,
        dbus::ObjectPath(chromeos::kChromeReportingServicePath)));

    upload_client_ = UploadClientProducer::CreateForTests(
        mock_bus_, mock_chrome_proxy_.get());
  }

  void TearDown() override {
    // Let everything ongoing to finish.
    task_environment_.RunUntilIdle();
  }

  static void CreateMockDBus(
      base::OnceCallback<void(scoped_refptr<dbus::MockBus>)> ready_cb) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    std::move(ready_cb).Run(
        base::WrapRefCounted<dbus::MockBus>(new dbus::MockBus(options)));
  }

  void AssertOnDBusThread() {
    ASSERT_TRUE(dbus_task_runner_->RunsTasksInCurrentSequence());
  }

  base::test::TaskEnvironment task_environment_;

  scoped_refptr<base::SequencedTaskRunner> dbus_task_runner_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_chrome_proxy_;
  scoped_refptr<UploadClient> upload_client_;
};

TEST_F(UploadClientTest, SuccessfulCall) {
  test::TestCallbackWaiter waiter;
  waiter.Attach();
  auto response_callback = base::BindOnce(
      [](test::TestCallbackWaiter* waiter,
         StatusOr<UploadEncryptedRecordResponse> response) {
        ASSERT_TRUE(response.ok());
        UploadEncryptedRecordResponse upload_response =
            std::move(response.ValueOrDie());
        EXPECT_EQ(upload_response.status().code(), error::OK);
        waiter->Signal();
      },
      &waiter);

  constexpr char kTestData[] = "TEST_DATA";
  EncryptedRecord encrypted_record;
  encrypted_record.set_encrypted_wrapped_record(kTestData);

  const int64_t kSequenceId = 42;
  const int64_t kGenerationId = 1701;
  const Priority kPriority = Priority::SLOW_BATCH;
  SequenceInformation* sequence_information =
      encrypted_record.mutable_sequence_information();
  sequence_information->set_sequencing_id(kSequenceId);
  sequence_information->set_generation_id(kGenerationId);
  sequence_information->set_priority(kPriority);

  // We have to own the response here so that it lives throughout the rest of
  // the test.
  std::unique_ptr<dbus::Response> dbus_response = dbus::Response::CreateEmpty();

  EXPECT_CALL(*mock_chrome_proxy_, DoCallMethod(_, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke([&encrypted_record, &dbus_response](
                                          dbus::MethodCall* call,
                                          base::OnceCallback<void(
                                              // clang-format off
                                              dbus::Response* response)>*
                                              // clang-format on
                                              response_cb) {
        ASSERT_NE(call, nullptr);
        ASSERT_THAT(call->GetInterface(),
                    Eq(chromeos::kChromeReportingServiceInterface));
        ASSERT_THAT(
            call->GetMember(),
            Eq(chromeos::kChromeReportingServiceUploadEncryptedRecordMethod));

        // Read the request.
        dbus::MessageReader reader(call);
        UploadEncryptedRecordRequest request;
        ASSERT_TRUE(reader.PopArrayOfBytesAsProto(&request));

        // We only expect the one record, so access it directly.
        std::string requested_serialized;
        request.encrypted_record(0).SerializeToString(&requested_serialized);

        std::string expected_serialized;
        encrypted_record.SerializeToString(&expected_serialized);
        EXPECT_THAT(requested_serialized, StrEq(expected_serialized));

        UploadEncryptedRecordResponse upload_response;
        upload_response.mutable_status()->set_code(error::OK);

        ASSERT_TRUE(dbus::MessageWriter(dbus_response.get())
                        .AppendProtoAsArrayOfBytes(upload_response));
        std::move(*response_cb).Run(dbus_response.get());
      })));

  std::unique_ptr<std::vector<EncryptedRecord>> records =
      std::make_unique<std::vector<EncryptedRecord>>();
  records->push_back(encrypted_record);
  upload_client_->SendEncryptedRecords(std::move(records),
                                       /*need_encryption_keys=*/false,
                                       std::move(response_callback));
  waiter.Wait();
}

}  // namespace
}  // namespace reporting
