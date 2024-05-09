// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_module.h"

#include <memory>
#include <string_view>
#include <utility>
#include <unistd.h>

#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "missive/compression/compression_module.h"
#include "missive/encryption/encryption_module.h"
#include "missive/encryption/verification.h"
#include "missive/health/health_module.h"
#include "missive/health/health_module_delegate_mock.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/resources/resource_manager.h"
#include "missive/storage/storage_base.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/server_configuration_controller.h"
#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/statusor.h"
#include "missive/util/test_support_callbacks.h"

namespace reporting {

// UploaderInterface for testing that replies with success every time.
class TestUploaderInterface : public UploaderInterface {
 public:
  TestUploaderInterface() = default;
  // Factory method.
  static void AsyncProvideUploader(
      UploaderInterface::UploadReason reason,
      UploaderInterface::InformAboutCachedUploadsCb inform_cb,
      UploaderInterfaceResultCb start_uploader_cb) {
    std::move(start_uploader_cb).Run(std::make_unique<TestUploaderInterface>());
  }

  void ProcessRecord(EncryptedRecord encrypted_record,
                     ScopedReservation scoped_reservation,
                     base::OnceCallback<void(bool)> processed_cb) override {
    // Reply with success
    std::move(processed_cb).Run(true);
  }

  void ProcessGap(SequenceInformation start,
                  uint64_t count,
                  base::OnceCallback<void(bool)> processed_cb) override {
    // Reply with success
    std::move(processed_cb).Run(true);
  }

  void Completed(Status final_status) override {
    // Do nothing
  }
};

class StorageModuleTest : public ::testing::Test {
 protected:
  StorageModuleTest() = default;

  void SetUp() override {
    ASSERT_TRUE(location_.CreateUniqueTempDir());
    options_.set_directory(location_.GetPath());
    storage_module_.reset();
  }

  void CreateStorageModule(std::string_view legacy_storage_enabled) {
    test::TestEvent<StatusOr<scoped_refptr<StorageModule>>> module_event;
    StorageModule::Create(
        {.options = options_,
         .legacy_storage_enabled = legacy_storage_enabled,
         .queues_container =
             QueuesContainer::Create(/*storage_degradation_enabled=*/false),
         .encryption_module = EncryptionModule::Create(/*is_enabled=*/false),
         .compression_module = CompressionModule::Create(
             /*is_enabled=*/true, /*compression_threshold=*/0,
             /*compression_type=*/CompressionInformation::COMPRESSION_SNAPPY),
         .health_module =
             HealthModule::Create(std::make_unique<HealthModuleDelegateMock>()),
         .server_configuration_controller =
             ServerConfigurationController::Create(/*is_enabled=*/true),
         .signature_verification_dev_flag =
             base::MakeRefCounted<SignatureVerificationDevFlag>(
                 /*is_enabled=*/false),
         .async_start_upload_cb =
             base::BindRepeating(TestUploaderInterface::AsyncProvideUploader)},
        base::BindPostTaskToCurrentDefault(module_event.cb()));
    auto res = module_event.result();
    ASSERT_OK(res);
    EXPECT_TRUE(res.value().get());
    storage_module_ = res.value();
  }

  Status CallAddRecord(scoped_refptr<StorageModule> module) {
    test::TestEvent<Status> event;
    Record record;
    record.set_data("DATA");
    record.set_destination(UPLOAD_EVENTS);
    record.set_dm_token("DM TOKEN");
    module->AddRecord(IMMEDIATE, std::move(record), event.cb());
    return event.result();
  }

  Status CallFlush(scoped_refptr<StorageModule> module) {
    test::TestEvent<Status> event;
    module->Flush(SECURITY, event.cb());
    return event.result();
  }

  void InjectStorageUnavailableError() {
    ASSERT_TRUE(storage_module_);
    storage_module_->InjectStorageUnavailableErrorForTesting();
  }

  void CreateStorageModuleWithBlockedDestinations(
      ListOfBlockedDestinations blocked_list) {
    test::TestEvent<StatusOr<scoped_refptr<StorageModule>>> module_event;
    // Initialize the server configuration controller.
    auto server_configuration_controller =
        ServerConfigurationController::Create(/*is_enabled=*/true);

    // Create and send a list of blocked destinations to the server
    // configuration controller. Provide a nullptr HealthModule::Recorder
    // since we are not tesing the upload of the health records.
    server_configuration_controller->UpdateConfiguration(
        blocked_list, HealthModule::Recorder(nullptr));

    StorageModule::Create(
        {.options = options_,
         .legacy_storage_enabled = "",
         .queues_container =
             QueuesContainer::Create(/*storage_degradation_enabled=*/false),
         .encryption_module = EncryptionModule::Create(/*is_enabled=*/false),
         .compression_module = CompressionModule::Create(
             /*is_enabled=*/true, /*compression_threshold=*/0,
             /*compression_type=*/
             CompressionInformation::COMPRESSION_SNAPPY),
         .health_module =
             HealthModule::Create(std::make_unique<HealthModuleDelegateMock>()),
         .server_configuration_controller = server_configuration_controller,
         .signature_verification_dev_flag =
             base::MakeRefCounted<SignatureVerificationDevFlag>(
                 /*is_enabled=*/false),
         .async_start_upload_cb =
             base::BindRepeating(TestUploaderInterface::AsyncProvideUploader)},
        base::BindPostTaskToCurrentDefault(module_event.cb()));
    auto res = module_event.result();
    ASSERT_OK(res);
    EXPECT_TRUE(res.value().get());
    storage_module_ = res.value();
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir location_;
  StorageOptions options_;
  scoped_refptr<HealthModule> health_module_;
  scoped_refptr<ServerConfigurationController> server_configuration_controller_;

  scoped_refptr<StorageModule> storage_module_;
};

TEST_F(StorageModuleTest, SwitchFromLegacyToStorage) {
  // Create storage module with IMMEDIATE and FAST_BACTH in single-generation
  // mode.
  CreateStorageModule("IMMEDIATE, FAST_BATCH");

  // Expect multi-/single-generational state match.
  EXPECT_TRUE(options_.is_multi_generational(SECURITY));
  EXPECT_FALSE(options_.is_multi_generational(IMMEDIATE));
  EXPECT_FALSE(options_.is_multi_generational(FAST_BATCH));
  EXPECT_TRUE(options_.is_multi_generational(SLOW_BATCH));

  ASSERT_OK(CallAddRecord(storage_module_));

  // Flip the value of multi-generation action flag to false, triggering
  // `storage_module` to switch IMMEDIATE (and other priorities except
  // SECURITY and SLOW_BATCH) from single-genration action to
  // multi-generation.
  storage_module_->SetLegacyEnabledPriorities("SECURITY, SLOW_BATCH");

  // Expect the module has indeed switched.
  EXPECT_FALSE(options_.is_multi_generational(SECURITY));
  EXPECT_TRUE(options_.is_multi_generational(IMMEDIATE));
  EXPECT_TRUE(options_.is_multi_generational(FAST_BATCH));
  EXPECT_FALSE(options_.is_multi_generational(SLOW_BATCH));

  // Verify we can write to new storage module after switching.
  ASSERT_OK(CallAddRecord(storage_module_));
}

TEST_F(StorageModuleTest, SwitchFromNewToLegacyStorage) {
  // Create storage module with new storage implementation.
  CreateStorageModule("SECURITY, FAST_BATCH");
  auto storage_module = storage_module_;

  // Expect multi-/single-generational state match.
  EXPECT_FALSE(options_.is_multi_generational(SECURITY));
  EXPECT_TRUE(options_.is_multi_generational(IMMEDIATE));
  EXPECT_FALSE(options_.is_multi_generational(FAST_BATCH));
  EXPECT_TRUE(options_.is_multi_generational(SLOW_BATCH));

  // Verify we can write to new storage module
  ASSERT_OK(CallAddRecord(storage_module));

  // Flip the value of multi-generation action flag to false, triggering
  // `storage_module` to switch IMMEDIATE and SECURITY
  // from multi-genration action to single-generation.
  storage_module_->SetLegacyEnabledPriorities("SECURITY, IMMEDIATE");

  // Expect that the storage module has indeed switched.
  EXPECT_FALSE(options_.is_multi_generational(SECURITY));
  EXPECT_FALSE(options_.is_multi_generational(IMMEDIATE));
  EXPECT_TRUE(options_.is_multi_generational(FAST_BATCH));
  EXPECT_TRUE(options_.is_multi_generational(SLOW_BATCH));

  // Verify we can write to legacy storage module after switching.
  ASSERT_OK(CallAddRecord(storage_module_));
}

TEST_F(StorageModuleTest, ExpectErrorIfStorageUnavailable) {
  CreateStorageModule("SECURITY");
  InjectStorageUnavailableError();

  const Status add_record_status = CallAddRecord(storage_module_);
  EXPECT_FALSE(add_record_status.ok());
  EXPECT_EQ(add_record_status.error_code(), error::UNAVAILABLE);

  const Status flush_status = CallFlush(storage_module_);
  EXPECT_FALSE(flush_status.ok());
  EXPECT_EQ(flush_status.error_code(), error::UNAVAILABLE);
}

TEST_F(StorageModuleTest, RecordBlockedByConfigFile) {
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(UPLOAD_EVENTS);
  CreateStorageModuleWithBlockedDestinations(blocked_list);
  const Status add_record_status = CallAddRecord(storage_module_);
  EXPECT_FALSE(add_record_status.ok());
  EXPECT_EQ(add_record_status.error_code(), error::CANCELLED);
}

TEST_F(StorageModuleTest, RecordNotBlockedByConfigFile) {
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(OS_EVENTS);
  CreateStorageModuleWithBlockedDestinations(blocked_list);
  ASSERT_OK(CallAddRecord(storage_module_));
}

TEST_F(StorageModuleTest, MultipleRecordsBlockedByConfigFile) {
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(UPLOAD_EVENTS);
  blocked_list.add_destinations(LOGIN_LOGOUT_EVENTS);
  CreateStorageModuleWithBlockedDestinations(blocked_list);

  // Checking UPLOAD_EVENTS destination. This should be blocked.
  const Status add_record_status = CallAddRecord(storage_module_);
  EXPECT_FALSE(add_record_status.ok());
  EXPECT_EQ(add_record_status.error_code(), error::CANCELLED);

  // Checking LOGIN_LOGOUT_EVENTS destination. This should be blocked.
  test::TestEvent<Status> login_event;
  Record login_record;
  login_record.set_data("DATA");
  login_record.set_destination(LOGIN_LOGOUT_EVENTS);
  login_record.set_dm_token("DM TOKEN");
  storage_module_->AddRecord(IMMEDIATE, std::move(login_record),
                             login_event.cb());
  const Status login_event_result = login_event.result();
  EXPECT_FALSE(login_event_result.ok());
  EXPECT_EQ(login_event_result.error_code(), error::CANCELLED);

  // Checking TELEMETRY_METRIC destination. This is not blocked.
  test::TestEvent<Status> telemetry_event;
  Record telemetry_record;
  telemetry_record.set_data("DATA");
  telemetry_record.set_destination(TELEMETRY_METRIC);
  telemetry_record.set_dm_token("DM TOKEN");
  storage_module_->AddRecord(IMMEDIATE, std::move(telemetry_record),
                             telemetry_event.cb());
  EXPECT_TRUE(telemetry_event.result().ok());
}
}  // namespace reporting
