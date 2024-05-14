// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/key_delivery.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/functional/callback_forward.h"
#include "missive/analytics/metrics.h"
#include "missive/analytics/metrics_test_util.h"
#include "missive/encryption/encryption_module.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/test_support_callbacks.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Property;
using ::testing::Return;
using ::testing::WithArg;

namespace reporting {
namespace {

class KeyDeliveryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    encryption_module_ = EncryptionModule::Create(
        /*is_enabled=*/true,
        /*renew_encryption_key_period=*/base::Minutes(30));
  }

  void TearDown() override {
    // Let key_delivery destruct on sequence
    task_environment_.RunUntilIdle();
  }

  ::testing::MockFunction<void(UploaderInterface::UploadReason,
                               UploaderInterface::InformAboutCachedUploadsCb,
                               UploaderInterface::UploaderInterfaceResultCb)>
      async_upload_start_;

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  scoped_refptr<EncryptionModuleInterface> encryption_module_;

  // Replace the metrics library instance with a mock one
  analytics::Metrics::TestEnvironment metrics_test_environment_;
};

class MockUploader : public UploaderInterface {
 public:
  static std::unique_ptr<MockUploader> Create(
      base::RepeatingClosure complete_cb) {
    auto uploader = std::make_unique<::testing::StrictMock<MockUploader>>();
    EXPECT_CALL(*uploader, ProcessRecord).Times(0);
    EXPECT_CALL(*uploader, ProcessGap).Times(0);
    EXPECT_CALL(*uploader, Completed(Eq(Status::StatusOK())))
        .WillOnce(Invoke([complete_cb]() { complete_cb.Run(); }));
    return uploader;
  }

  MockUploader() = default;
  MockUploader(const MockUploader&) = delete;
  MockUploader& operator=(const MockUploader&) = delete;

  MOCK_METHOD(void,
              ProcessRecord,
              (EncryptedRecord record,
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

TEST_F(KeyDeliveryTest, DeliveryOnRequest) {
  auto key_delivery = KeyDelivery::Create(
      encryption_module_,
      base::BindRepeating(
          &::testing::MockFunction<void(
              UploaderInterface::UploadReason,
              UploaderInterface::InformAboutCachedUploadsCb,
              UploaderInterface::UploaderInterfaceResultCb)>::Call,
          base::Unretained(&async_upload_start_)));

  EXPECT_CALL(async_upload_start_,
              Call(Eq(UploaderInterface::UploadReason::KEY_DELIVERY), _, _))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(base::BindRepeating(
                &KeyDelivery::OnCompletion,
                base::Unretained(key_delivery.get()), Status::StatusOK()));
            std::move(cb).Run(std::move(uploader));
          })));

  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(KeyDelivery::kResultUma, error::OK, error::MAX_VALUE))
      .WillOnce(Return(true))
      .RetiresOnSaturation();

  test::TestEvent<Status> key_event;
  key_delivery->Request(/*is_mandatory=*/true, key_event.cb());
  EXPECT_OK(key_event.result());
}

TEST_F(KeyDeliveryTest, FailedDeliveryOnRequest) {
  auto key_delivery = KeyDelivery::Create(
      encryption_module_,
      base::BindRepeating(
          &::testing::MockFunction<void(
              UploaderInterface::UploadReason,
              UploaderInterface::InformAboutCachedUploadsCb,
              UploaderInterface::UploaderInterfaceResultCb)>::Call,
          base::Unretained(&async_upload_start_)));

  EXPECT_CALL(async_upload_start_,
              Call(Eq(UploaderInterface::UploadReason::KEY_DELIVERY), _, _))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(
                base::BindRepeating(&KeyDelivery::OnCompletion,
                                    base::Unretained(key_delivery.get()),
                                    Status{error::CANCELLED, "For testing"}));
            std::move(cb).Run(std::move(uploader));
          })));

  EXPECT_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
              SendEnumToUMA(KeyDelivery::kResultUma, error::CANCELLED,
                            error::MAX_VALUE))
      .WillOnce(Return(true))
      .RetiresOnSaturation();

  test::TestEvent<Status> key_event;
  key_delivery->Request(/*is_mandatory=*/true, key_event.cb());
  EXPECT_THAT(key_event.result(),
              Property(&Status::error_code, Eq(error::CANCELLED)));
}

TEST_F(KeyDeliveryTest, PeriodicDelivery) {
  const auto kPeriod = base::Seconds(5);
  auto key_delivery = KeyDelivery::Create(
      encryption_module_,
      base::BindRepeating(
          &::testing::MockFunction<void(
              UploaderInterface::UploadReason,
              UploaderInterface::InformAboutCachedUploadsCb,
              UploaderInterface::UploaderInterfaceResultCb)>::Call,
          base::Unretained(&async_upload_start_)));

  EXPECT_CALL(async_upload_start_,
              Call(Eq(UploaderInterface::UploadReason::KEY_DELIVERY), _, _))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(
                base::BindRepeating(&KeyDelivery::OnCompletion,
                                    base::Unretained(key_delivery.get()),
                                    Status{error::CANCELLED, "For testing"}));
            std::move(cb).Run(std::move(uploader));
          })))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(base::BindRepeating(
                &KeyDelivery::OnCompletion,
                base::Unretained(key_delivery.get()), Status::StatusOK()));
            std::move(cb).Run(std::move(uploader));
          })));
  key_delivery->StartPeriodicKeyUpdate(kPeriod);

  EXPECT_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
              SendEnumToUMA(KeyDelivery::kResultUma, error::CANCELLED,
                            error::MAX_VALUE))
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(KeyDelivery::kResultUma, error::OK, error::MAX_VALUE))
      .WillOnce(Return(true))
      .RetiresOnSaturation();

  task_environment_.FastForwardBy(2 * kPeriod);
}
}  // namespace
}  // namespace reporting
