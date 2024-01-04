// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ext4_container.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/files/file_path.h>

#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/fake_encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace cryptohome {

class Ext4ContainerTest : public ::testing::Test {
 public:
  Ext4ContainerTest()
      : config_({.mkfs_opts = {"-O", "encrypt,verity"},
                 .tune2fs_opts = {"-Q", "project"},
                 .backend_type = EncryptedContainerType::kDmcrypt}),
        key_({.fek = brillo::SecureBlob("random key")}),
        backing_container_(std::make_unique<FakeEncryptedContainer>(
            config_.backend_type, base::FilePath("/dev/mapper/encstateful"))) {}
  ~Ext4ContainerTest() override = default;

  void GenerateContainer() {
    container_ = std::make_unique<Ext4Container>(
        config_, std::move(backing_container_), &platform_);
  }

 protected:
  Ext4FileSystemConfig config_;
  FileSystemKey key_;
  MockPlatform platform_;
  std::unique_ptr<Ext4Container> container_;
  std::unique_ptr<FakeEncryptedContainer> backing_container_;
};

// Tests the creation path for the dm-crypt container.
TEST_F(Ext4ContainerTest, SetupCreateCheck) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));

  GenerateContainer();
  EXPECT_TRUE(container_->Setup(key_));
}

// Tests the setup path with an existing container.
TEST_F(Ext4ContainerTest, SetupNoCreateCheck) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).Times(0);
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));

  backing_container_->Setup(key_);
  GenerateContainer();
  EXPECT_TRUE(container_->Setup(key_));
}

// Tests failure path if the filesystem setup fails.
TEST_F(Ext4ContainerTest, SetupFailedFormatExt4) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).Times(0);

  GenerateContainer();
  EXPECT_FALSE(container_->Setup(key_));
}

// Tests failure path if the filesystem tune2fs fails just after creation.
TEST_F(Ext4ContainerTest, SetupFailedTune2FsAfterFormatExt4) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(false));

  GenerateContainer();
  EXPECT_FALSE(container_->Setup(key_));
}

// Tests that the dmcrypt container cannot be reset if it is set up with a
// filesystem.
TEST_F(Ext4ContainerTest, ResetFileSystemContainerTest) {
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));

  backing_container_->Setup(key_);
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  // Attempt a reset of the device.
  EXPECT_FALSE(container_->Reset());
  EXPECT_TRUE(container_->Teardown());
}
}  // namespace cryptohome
