// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_module.h"

#include <base/functional/callback_helpers.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "missive/compression/compression_module.h"
#include "missive/encryption/encryption_module.h"
#include "missive/util/test_support_callbacks.h"

namespace reporting {

namespace {

class StorageModuleTest : public ::testing::Test {
 protected:
  StorageModuleTest() = default;

  base::test::TaskEnvironment task_environment_;
};

TEST_F(StorageModuleTest, NewStorageTest) {
  test::TestEvent<StatusOr<scoped_refptr<StorageModule>>> module_event;
  StorageModule::Create(
      StorageOptions(),
      /*legacy_storage_enabled=*/false, base::DoNothing(),
      QueuesContainer::Create(/*is_enabled=*/false),
      EncryptionModule::Create(/*is_enabled=*/true),
      CompressionModule::Create(
          /*is_enabled=*/true, /*compression_threshold=*/0,
          /*compression_type=*/CompressionInformation::COMPRESSION_SNAPPY),
      module_event.cb());
  const auto res = module_event.result();
  ASSERT_OK(res);
  EXPECT_TRUE(res.ValueOrDie().get());
  EXPECT_FALSE(res.ValueOrDie()->legacy_storage_enabled());
}

TEST_F(StorageModuleTest, LegacyStorageTest) {
  test::TestEvent<StatusOr<scoped_refptr<StorageModule>>> module_event;
  StorageModule::Create(
      StorageOptions(),
      /*legacy_storage_enabled=*/true, base::DoNothing(),
      QueuesContainer::Create(/*is_enabled=*/false),
      EncryptionModule::Create(/*is_enabled=*/true),
      CompressionModule::Create(
          /*is_enabled=*/true, /*compression_threshold=*/0,
          /*compression_type=*/CompressionInformation::COMPRESSION_SNAPPY),
      module_event.cb());
  const auto res = module_event.result();
  ASSERT_OK(res);
  EXPECT_TRUE(res.ValueOrDie().get());
  EXPECT_TRUE(res.ValueOrDie()->legacy_storage_enabled());
}

// TOOD(b/279057326): Once dynamic switching is implemented,
// add respective tests.

}  // namespace
}  // namespace reporting
