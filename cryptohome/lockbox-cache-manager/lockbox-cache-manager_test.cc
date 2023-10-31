// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/lockbox-cache-manager/lockbox-cache-manager.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/lockbox-cache-manager/mock_platform.h"

using ::testing::_;
using ::testing::Mock;
using ::testing::Return;
using ::testing::StrictMock;

namespace cryptohome {

namespace {
// Helper to create a test file.
bool CreateFile(const base::FilePath& file_path, std::string_view content) {
  if (!base::CreateDirectory(file_path.DirName()))
    return false;
  return base::WriteFile(file_path, content.data(), content.size()) ==
         content.size();
}
}  // namespace

class LockboxCacheManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();
    platform_ = mock_platform.get();
    manager_ = std::make_unique<LockboxCacheManager>(base_dir_);
    manager_->SetParamsForTesting(std::move(mock_platform));
    ASSERT_TRUE(
        base::CreateDirectory(manager_->GetLockboxCachePath().DirName()));
  }

 protected:
  std::unique_ptr<LockboxCacheManager> manager_;
  StrictMock<MockPlatform>* platform_;
  base::FilePath base_dir_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(LockboxCacheManagerTest, MigrationNotNeeded) {
  EXPECT_EQ(MigrationStatus::kNotNeeded,
            manager_->MigrateInstallAttributesIfNeeded());
}

TEST_F(LockboxCacheManagerTest, MigrationSuccess) {
  ASSERT_TRUE(CreateFile(manager_->GetInstallAttrsOldPath(), "foobar"));
  EXPECT_EQ(MigrationStatus::kSuccess,
            manager_->MigrateInstallAttributesIfNeeded());
}

TEST_F(LockboxCacheManagerTest,
       MigrationSuccessInstallAttributesExistInBothLocation) {
  ASSERT_TRUE(CreateFile(manager_->GetInstallAttrsOldPath(), "foobar"));
  ASSERT_TRUE(CreateFile(manager_->GetInstallAttrsNewPath(), "foobar"));
  EXPECT_EQ(MigrationStatus::kSuccess,
            manager_->MigrateInstallAttributesIfNeeded());
}

#if USE_TPM_DYNAMIC

TEST_F(LockboxCacheManagerTest, LockboxCacheCreationSuccessNoTpm) {
  // Create an empty nvram file
  ASSERT_TRUE(CreateFile(manager_->GetLockboxNvramFilePath(), ""));
  ASSERT_TRUE(CreateFile(manager_->GetInstallAttrsNewPath(), "foobar"));
  EXPECT_TRUE(manager_->CreateLockboxCache());
}

TEST_F(LockboxCacheManagerTest, LockboxCacheCreationSuccessTpmDynamic) {
  // Create a non-empty nvram file
  ASSERT_TRUE(CreateFile(manager_->GetLockboxNvramFilePath(), "foobar"));
  ASSERT_TRUE(CreateFile(manager_->GetInstallAttrsNewPath(), "foobar"));

  std::vector<std::string> argv = {
      "lockbox-cache", "--nvram=" + manager_->GetLockboxNvramFilePath().value(),
      "--cache=" + manager_->GetLockboxCachePath().value(),
      "--lockbox=" + manager_->GetInstallAttrsNewPath().value()};
  EXPECT_CALL(*platform_, GetAppOutputAndError(argv, _)).WillOnce(Return(true));

  EXPECT_TRUE(manager_->CreateLockboxCache());

  // First install, no nvram contents
  ASSERT_TRUE(brillo::DeleteFile(manager_->GetLockboxNvramFilePath()));
  EXPECT_TRUE(manager_->CreateLockboxCache());
}

#else

TEST_F(LockboxCacheManagerTest, LockboxCacheCreationSuccessNormalDevice) {
  // Create a non-empty nvram file
  ASSERT_TRUE(CreateFile(manager_->GetLockboxNvramFilePath(), "foobar"));
  EXPECT_CALL(*platform_,
              IsOwnedByRoot(manager_->GetLockboxNvramFilePath().value()))
      .WillOnce(Return(true));
  std::vector<std::string> argv = {
      "lockbox-cache", "--nvram=" + manager_->GetLockboxNvramFilePath().value(),
      "--cache=" + manager_->GetLockboxCachePath().value(),
      "--lockbox=" + manager_->GetInstallAttrsNewPath().value()};
  EXPECT_CALL(*platform_, GetAppOutputAndError(argv, _)).WillOnce(Return(true));

  EXPECT_TRUE(manager_->CreateLockboxCache());

  // First install, no nvram contents
  ASSERT_TRUE(brillo::DeleteFile(manager_->GetLockboxNvramFilePath()));
  EXPECT_TRUE(manager_->CreateLockboxCache());
}

TEST_F(LockboxCacheManagerTest, LockboxCacheCreationFailureNormalDevice) {
  // Create a non-empty nvram file
  ASSERT_TRUE(CreateFile(manager_->GetLockboxNvramFilePath(), "foobar"));
  EXPECT_CALL(*platform_,
              IsOwnedByRoot(manager_->GetLockboxNvramFilePath().value()))
      .WillOnce(Return(false));
  EXPECT_FALSE(manager_->CreateLockboxCache());
}

#endif

}  // namespace cryptohome
