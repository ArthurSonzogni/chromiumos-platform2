// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/key_delivery.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/memory/scoped_refptr.h>
#include <base/rand_util.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/analytics/metrics.h"
#include "missive/analytics/metrics_test_util.h"
#include "missive/encryption/encryption_module.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/encryption/primitives.h"
#include "missive/encryption/testing_primitives.h"
#include "missive/storage/storage_configuration.h"
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
      StorageOptions::kDefaultKeyCheckPeriod,
      StorageOptions::kLazyDefaultKeyCheckPeriod, encryption_module_,
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
                &KeyDelivery::OnKeyUpdateResult,
                base::Unretained(key_delivery.get()), Status::StatusOK()));
            std::move(cb).Run(std::move(uploader));
          })));

  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(KeyDelivery::kResultUma, error::OK, error::MAX_VALUE))
      .WillOnce(Return(true))
      .RetiresOnSaturation();

  test::TestEvent<Status> key_event;
  key_delivery->Request(key_event.cb());
  EXPECT_OK(key_event.result());
}

TEST_F(KeyDeliveryTest, FailedDeliveryOnRequest) {
  auto key_delivery = KeyDelivery::Create(
      StorageOptions::kDefaultKeyCheckPeriod,
      StorageOptions::kLazyDefaultKeyCheckPeriod, encryption_module_,
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
                base::BindRepeating(&KeyDelivery::OnKeyUpdateResult,
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
  key_delivery->Request(key_event.cb());
  EXPECT_THAT(key_event.result(),
              Property(&Status::error_code, Eq(error::CANCELLED)));
}

TEST_F(KeyDeliveryTest, PeriodicDelivery) {
  auto key_delivery = KeyDelivery::Create(
      StorageOptions::kDefaultKeyCheckPeriod,
      StorageOptions::kLazyDefaultKeyCheckPeriod, encryption_module_,
      base::BindRepeating(
          &::testing::MockFunction<void(
              UploaderInterface::UploadReason,
              UploaderInterface::InformAboutCachedUploadsCb,
              UploaderInterface::UploaderInterfaceResultCb)>::Call,
          base::Unretained(&async_upload_start_)));

  // Observe one key delivery.
  EXPECT_CALL(async_upload_start_,
              Call(Eq(UploaderInterface::UploadReason::KEY_DELIVERY), _, _))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(
                base::BindRepeating(&KeyDelivery::OnKeyUpdateResult,
                                    base::Unretained(key_delivery.get()),
                                    Status{error::CANCELLED, "For testing"}));
            std::move(cb).Run(std::move(uploader));
          })))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(base::BindRepeating(
                &KeyDelivery::OnKeyUpdateResult,
                base::Unretained(key_delivery.get()), Status::StatusOK()));
            std::move(cb).Run(std::move(uploader));
          })));

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
  // Start periodic updates, like `Storage` does when key is found.
  key_delivery->ScheduleNextKeyUpdate();
  task_environment_.FastForwardBy(2 * StorageOptions::kDefaultKeyCheckPeriod);

  // Record a new key.
  uint8_t out_public_value[kKeySize];
  uint8_t out_private_key[kKeySize];
  // Generate new pair of private key and public value.
  test::GenerateEncryptionKeyPair(out_private_key, out_public_value);
  const uint32_t public_key_id =
      base::RandGenerator(std::numeric_limits<Encryptor::PublicKeyId>::max());

  test::TestEvent<Status> set_public_key;
  encryption_module_->UpdateAsymmetricKey(
      std::string(reinterpret_cast<const char*>(out_public_value), kKeySize),
      public_key_id, set_public_key.cb());
  ASSERT_OK(set_public_key.result());

  // Observe no more deliveries after the key has been recorded.
  EXPECT_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
              SendEnumToUMA(KeyDelivery::kResultUma, _, error::MAX_VALUE))
      .Times(0);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);

  // Observe one more key delivery after a lazy check period.
  EXPECT_CALL(async_upload_start_,
              Call(Eq(UploaderInterface::UploadReason::KEY_DELIVERY), _, _))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(base::BindRepeating(
                &KeyDelivery::OnKeyUpdateResult,
                base::Unretained(key_delivery.get()), Status::StatusOK()));
            std::move(cb).Run(std::move(uploader));
          })));
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(KeyDelivery::kResultUma, error::OK, error::MAX_VALUE))
      .WillOnce(Return(true))
      .RetiresOnSaturation();

  task_environment_.FastForwardBy(StorageOptions::kLazyDefaultKeyCheckPeriod);
}

TEST_F(KeyDeliveryTest, ImplicitPeriodicDelivery) {
  auto key_delivery = KeyDelivery::Create(
      StorageOptions::kDefaultKeyCheckPeriod,
      StorageOptions::kLazyDefaultKeyCheckPeriod, encryption_module_,
      base::BindRepeating(
          &::testing::MockFunction<void(
              UploaderInterface::UploadReason,
              UploaderInterface::InformAboutCachedUploadsCb,
              UploaderInterface::UploaderInterfaceResultCb)>::Call,
          base::Unretained(&async_upload_start_)));

  // Observe one key delivery.
  EXPECT_CALL(async_upload_start_,
              Call(Eq(UploaderInterface::UploadReason::KEY_DELIVERY), _, _))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(base::BindRepeating(
                &KeyDelivery::OnKeyUpdateResult,
                base::Unretained(key_delivery.get()), Status::StatusOK()));
            std::move(cb).Run(std::move(uploader));
          })))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(
                base::BindRepeating(&KeyDelivery::OnKeyUpdateResult,
                                    base::Unretained(key_delivery.get()),
                                    Status{error::CANCELLED, "For testing"}));
            std::move(cb).Run(std::move(uploader));
          })))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(base::BindRepeating(
                &KeyDelivery::OnKeyUpdateResult,
                base::Unretained(key_delivery.get()), Status::StatusOK()));
            std::move(cb).Run(std::move(uploader));
          })));

  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(KeyDelivery::kResultUma, error::OK, error::MAX_VALUE))
      .WillOnce(Return(true))
      .RetiresOnSaturation();

  // Request key and start periodic updates, like `Storage` does when key is not
  // found.
  test::TestEvent<Status> key_event;
  key_delivery->Request(key_event.cb());
  EXPECT_OK(key_event.result());

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

  task_environment_.FastForwardBy(2 * StorageOptions::kDefaultKeyCheckPeriod);
  // Record a new key.
  uint8_t out_public_value[kKeySize];
  uint8_t out_private_key[kKeySize];
  // Generate new pair of private key and public value.
  test::GenerateEncryptionKeyPair(out_private_key, out_public_value);
  const uint32_t public_key_id =
      base::RandGenerator(std::numeric_limits<Encryptor::PublicKeyId>::max());

  test::TestEvent<Status> set_public_key;
  encryption_module_->UpdateAsymmetricKey(
      std::string(reinterpret_cast<const char*>(out_public_value), kKeySize),
      public_key_id, set_public_key.cb());
  ASSERT_OK(set_public_key.result());

  // Observe no more deliveries after the key has been recorded.
  EXPECT_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
              SendEnumToUMA(KeyDelivery::kResultUma, _, error::MAX_VALUE))
      .Times(0);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);
  task_environment_.FastForwardBy(StorageOptions::kDefaultKeyCheckPeriod);

  // Observe one more key delivery after a lazy check period.
  EXPECT_CALL(async_upload_start_,
              Call(Eq(UploaderInterface::UploadReason::KEY_DELIVERY), _, _))
      .WillOnce(WithArg<2>(Invoke(
          [&key_delivery](UploaderInterface::UploaderInterfaceResultCb cb) {
            auto uploader = MockUploader::Create(base::BindRepeating(
                &KeyDelivery::OnKeyUpdateResult,
                base::Unretained(key_delivery.get()), Status::StatusOK()));
            std::move(cb).Run(std::move(uploader));
          })));
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(KeyDelivery::kResultUma, error::OK, error::MAX_VALUE))
      .WillOnce(Return(true))
      .RetiresOnSaturation();

  task_environment_.FastForwardBy(StorageOptions::kLazyDefaultKeyCheckPeriod);
}

TEST_F(KeyDeliveryTest, ExpirationWhileRequestsPending) {
  auto key_delivery = KeyDelivery::Create(
      StorageOptions::kDefaultKeyCheckPeriod,
      StorageOptions::kLazyDefaultKeyCheckPeriod, encryption_module_,
      base::BindRepeating(
          &::testing::MockFunction<void(
              UploaderInterface::UploadReason,
              UploaderInterface::InformAboutCachedUploadsCb,
              UploaderInterface::UploaderInterfaceResultCb)>::Call,
          base::Unretained(&async_upload_start_)));

  EXPECT_CALL(async_upload_start_,
              Call(Eq(UploaderInterface::UploadReason::KEY_DELIVERY), _, _))
      .Times(1);

  EXPECT_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
              SendEnumToUMA)
      .Times(0);

  // Request key and discard `key_delivery`.
  test::TestEvent<Status> key_event;
  key_delivery->Request(key_event.cb());
  key_delivery.reset();
  EXPECT_THAT(key_event.result(),
              Property(&Status::error_code, Eq(error::UNAVAILABLE)));
}
}  // namespace
}  // namespace reporting
