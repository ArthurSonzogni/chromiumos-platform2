// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_module.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
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
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/resources/resource_manager.h"
#include "missive/storage/new_storage.h"
#include "missive/storage/storage_base.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
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

  void SetUp() override { storage_module_.reset(); }

  void CreateStorageModule(base::StringPiece legacy_storage_enabled) {
    test::TestEvent<StatusOr<scoped_refptr<StorageModule>>> module_event;
    StorageModule::Create(
        options_, legacy_storage_enabled,
        base::BindRepeating(TestUploaderInterface::AsyncProvideUploader),
        QueuesContainer::Create(/*is_enabled=*/false),
        EncryptionModule::Create(/*is_enabled=*/false),
        CompressionModule::Create(
            /*is_enabled=*/true, /*compression_threshold=*/0,
            /*compression_type=*/CompressionInformation::COMPRESSION_SNAPPY),
        base::MakeRefCounted<SignatureVerificationDevFlag>(
            /*is_enabled=*/false),
        base::BindPostTaskToCurrentDefault(module_event.cb()));
    auto res = module_event.result();
    ASSERT_OK(res);
    EXPECT_TRUE(res.ValueOrDie().get());
    storage_module_ = res.ValueOrDie();
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

  base::test::TaskEnvironment task_environment_;
  StorageOptions options_;
  scoped_refptr<StorageModule> storage_module_;
};

TEST_F(StorageModuleTest, SwitchFromLegacyToNewStorage) {
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
  // `storage_module` to switch IMMEDIATE (and other priorities except SECURITY
  // and SLOW_BATCH) from single-genration action to multi-generation.
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
  auto new_storage_module = storage_module_;

  // Expect multi-/single-generational state match.
  EXPECT_FALSE(options_.is_multi_generational(SECURITY));
  EXPECT_TRUE(options_.is_multi_generational(IMMEDIATE));
  EXPECT_FALSE(options_.is_multi_generational(FAST_BATCH));
  EXPECT_TRUE(options_.is_multi_generational(SLOW_BATCH));

  // Verify we can write to new storage module
  ASSERT_OK(CallAddRecord(new_storage_module));

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
}  // namespace reporting
