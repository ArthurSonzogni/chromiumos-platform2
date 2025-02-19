// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/ext4_container.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/mock_platform.h>
#include <metrics/metrics_library_mock.h>

#include "libstorage/storage_container/dmsetup_container.h"
#include "libstorage/storage_container/fake_storage_container.h"
#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/partition_device.h"
#include "libstorage/storage_container/storage_container.h"
#include "libstorage/storage_container/unencrypted_container.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace libstorage {

class Ext4ContainerTest : public ::testing::Test {
 public:
  Ext4ContainerTest()
      : config_({.mkfs_opts = {"-O", "encrypt,verity"},
                 .tune2fs_opts = {"-Q", "project"},
                 .backend_type = StorageContainerType::kDmcrypt,
                 .recovery = RecoveryType::kEnforceCleaning,
                 .metrics_prefix = "MetricsTested"}),
        key_({.fek = brillo::SecureBlob("random key")}),
        container_name_(base::FilePath("/dev/mapper/encstateful")),
        backing_container_(std::make_unique<FakeStorageContainer>(
            config_.backend_type, container_name_)) {}
  ~Ext4ContainerTest() override = default;

  // Setup is not used, to be able to create the backing_container_ beforehand
  // or not.
  void GenerateContainer() {
    backing_container_ptr_ = backing_container_.get();
    container_ = std::make_unique<Ext4Container>(
        config_, std::move(backing_container_), &platform_, &metrics_);
    ASSERT_TRUE(
        platform_.WriteStringToFile(container_name_, std::string(2048, 0)));
  }

 protected:
  MetricsLibraryMock metrics_;
  Ext4FileSystemConfig config_;
  FileSystemKey key_;
  base::FilePath container_name_;
  MockPlatform platform_;
  std::unique_ptr<Ext4Container> container_;
  std::unique_ptr<FakeStorageContainer> backing_container_;
  FakeStorageContainer* backing_container_ptr_;
};

// Tests the creation path for the dm-crypt container.
TEST_F(Ext4ContainerTest, SetupCreateCheck) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Fsck(_, _, _)).Times(0);
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));

  GenerateContainer();
  EXPECT_TRUE(container_->Setup(key_));
}

// Tests the setup path with an existing container.
TEST_F(Ext4ContainerTest, SetupNoCreateCheck) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).Times(0);
  EXPECT_CALL(platform_, Fsck(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(0), Return(true)));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_RecoveryNeeded", false))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_FsckNeeded", false))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 2, 10))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendToUMA("MetricsTested_ErrorCount", 0, 0, 100000, 20))
      .WillOnce(Return(true));

  backing_container_->Setup(key_);
  GenerateContainer();
  EXPECT_TRUE(container_->Setup(key_));
}

// Tests the creation path for the dm-crypt container.
// Invalid settings.
TEST_F(Ext4ContainerTest, SetupCreateCheckTune2FsError) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Fsck(_, _, _)).Times(0);
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(false));

  GenerateContainer();
  EXPECT_FALSE(container_->Setup(key_));
}

// Tests failure path if the filesystem setup fails.
TEST_F(Ext4ContainerTest, SetupFailedFormatExt4) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, Fsck(_, _, _)).Times(0);
  EXPECT_CALL(platform_, Tune2Fs(_, _)).Times(0);

  GenerateContainer();
  EXPECT_FALSE(container_->Setup(key_));
}

// Tests failure path if the filesystem tune2fs fails just after creation.
// FSCK not called.
TEST_F(Ext4ContainerTest, SetupFailedTune2FsAfterFormatExt4) {
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Fsck(_, _, _)).Times(0);
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(false));

  GenerateContainer();
  EXPECT_FALSE(container_->Setup(key_));
}

// Tests the failure path on checking the filesystem.
TEST_F(Ext4ContainerTest, SetupFailedFsck) {
  EXPECT_CALL(platform_, Fsck(_, FsckOption::kPreen, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(FSCK_ERRORS_LEFT_UNCORRECTED), Return(false)));
  EXPECT_CALL(platform_, Fsck(_, FsckOption::kFull, _))
      .WillOnce(DoAll(SetArgPointee<2>(
                          FSCK_ERRORS_LEFT_UNCORRECTED | FSCK_SHARED_LIB_ERROR |
                          FSCK_SYSTEM_SHOULD_REBOOT | FSCK_OPERATIONAL_ERROR),
                      Return(false)));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_RecoveryNeeded", true))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_FsckNeeded", true))
      .WillOnce(Return(true));
  // Mock only available for the int version.
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 1, 10))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 4, 10))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 5, 10))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 6, 10))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 9, 10))
      .WillOnce(Return(true));
  backing_container_->Setup(key_);
  GenerateContainer();
  EXPECT_FALSE(container_->Setup(key_));
}

// Tests the failure path on setting new filesystem features.
// No recovery required.
TEST_F(Ext4ContainerTest, SetupFailedTune2fsDontCare) {
  EXPECT_CALL(platform_, Fsck(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(FSCK_ERRORS_LEFT_UNCORRECTED), Return(false)));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_RecoveryNeeded", true))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_FsckNeeded", true))
      .WillOnce(Return(true));
  // Mock only available for the int version.
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 5, 10))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(false));
  EXPECT_CALL(metrics_, SendToUMA("MetricsTested_ErrorCount", 0, 0, 100000, 20))
      .WillOnce(Return(true));

  backing_container_->Setup(key_);
  config_.recovery = RecoveryType::kDoNothing;
  GenerateContainer();
  EXPECT_TRUE(container_->Setup(key_));
}

// Tests the failure path on setting new filesystem features.
// Recovery required.
TEST_F(Ext4ContainerTest, SetupFailedTune2fsFsckFailed) {
  EXPECT_CALL(platform_, Fsck(_, _, _))
      .Times(2)
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(FSCK_ERRORS_LEFT_UNCORRECTED), Return(false)));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(false));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_RecoveryNeeded", true))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_FsckNeeded", true))
      .WillOnce(Return(true));
  // Mock only available for the int version.
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 5, 10))
      .WillOnce(Return(true));
  backing_container_->Setup(key_);
  GenerateContainer();
  EXPECT_FALSE(container_->Setup(key_));
}

// Tests the failure path on failing fsck where purge is enforced.
TEST_F(Ext4ContainerTest, SetupFailedFsckPurge) {
  EXPECT_CALL(platform_, Fsck(_, FsckOption::kPreen, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(FSCK_ERRORS_LEFT_UNCORRECTED), Return(false)));

  // Check we purge and recreate.
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));
  backing_container_->Setup(key_);
  config_.recovery = RecoveryType::kPurge;
  GenerateContainer();
  EXPECT_TRUE(container_->Setup(key_));
  // Check the underlying container has been recreated.
  EXPECT_TRUE(backing_container_ptr_->Exists());
}

// Tests the recovery path when running deep fsck, recovery required.
TEST_F(Ext4ContainerTest, SetupFailedTune2fsFsckFixed) {
  InSequence s;
  EXPECT_CALL(platform_, Fsck(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(FSCK_ERRORS_LEFT_UNCORRECTED), Return(false)));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_FsckNeeded", true))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendBoolToUMA("MetricsTested_RecoveryNeeded", true))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Fsck(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(FSCK_ERROR_CORRECTED), Return(true)));
  EXPECT_CALL(metrics_, SendEnumToUMA("MetricsTested.fsckResult", 3, 10))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));
  EXPECT_CALL(metrics_, SendToUMA("MetricsTested_ErrorCount", 0, 0, 100000, 20))
      .WillOnce(Return(true));

  backing_container_->Setup(key_);
  GenerateContainer();
  EXPECT_TRUE(container_->Setup(key_));
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

class Ext4ContainerPartitionTest : public ::testing::Test {
 public:
  Ext4ContainerPartitionTest() {}

  void SetUp() {
    ASSERT_TRUE(platform_.WriteStringToFile(partition_file_, ""));
    base::stat_wrapper_t st;
    st.st_mode = S_IFBLK;
    EXPECT_CALL(platform_, Stat(partition_file_, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(st), Return(true)));

    std::unique_ptr<PartitionDevice> backing_device;
    backing_device = std::make_unique<PartitionDevice>(
        config_.unencrypted_config.backing_device_config, &platform_);
    std::unique_ptr<UnencryptedContainer> backing_container;
    backing_container = std::make_unique<UnencryptedContainer>(
        std::move(backing_device), &platform_);
    container_ = std::make_unique<Ext4Container>(config_.filesystem_config,
                                                 std::move(backing_container),
                                                 &platform_, nullptr);
  }

 protected:
  base::FilePath partition_file_{"/dev/mmcp10"};
  libstorage::StorageContainerConfig config_{
      .filesystem_config = {.tune2fs_opts = {"-Q", "project"},
                            .backend_type = StorageContainerType::kUnencrypted,
                            .recovery = RecoveryType::kDoNothing},
      .unencrypted_config = {
          .backing_device_config = {
              .type = libstorage::BackingDeviceType::kPartition,
              .name = partition_file_.value()}}};
  MockPlatform platform_;
  std::unique_ptr<Ext4Container> container_;
};

// Tests the normal path.
TEST_F(Ext4ContainerPartitionTest, SetupCreateCheck) {
  EXPECT_CALL(platform_, FormatExt4(partition_file_, _, _)).Times(0);
  int fsck_err = 0;
  EXPECT_CALL(platform_, Fsck(partition_file_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(fsck_err), Return(false)));
  EXPECT_CALL(platform_, Tune2Fs(partition_file_, _)).WillOnce(Return(true));

  // Make sure we can read the superblock.
  ASSERT_TRUE(
      platform_.WriteStringToFile(partition_file_, std::string(2048, 0)));

  FileSystemKey key;
  EXPECT_TRUE(container_->Setup(key));
}

TEST_F(Ext4ContainerPartitionTest, SetupNoSuperblock) {
  EXPECT_CALL(platform_, FormatExt4(partition_file_, _, _)).Times(0);
  int fsck_err = 0;
  EXPECT_CALL(platform_, Fsck(partition_file_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(fsck_err), Return(false)));
  EXPECT_CALL(platform_, Tune2Fs(partition_file_, _)).WillOnce(Return(true));

  FileSystemKey key;

  // superblock is too small, will fail.
  EXPECT_FALSE(container_->Setup(key));
}

class Ext4ContainerDmTest : public ::testing::Test {
 public:
  Ext4ContainerDmTest() {}

  void SetUp() {
    ASSERT_TRUE(platform_.WriteStringToFile(partition_file_, ""));
    base::stat_wrapper_t st;
    st.st_mode = S_IFBLK;
    EXPECT_CALL(platform_, Stat(partition_file_, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(st), Return(true)));

    std::unique_ptr<PartitionDevice> backing_device;
    backing_device = std::make_unique<PartitionDevice>(
        config_.dmsetup_config.backing_device_config, &platform_);
    FileSystemKeyReference key_reference(
        {.fek_sig = brillo::SecureBlob("random reference")});
    std::unique_ptr<DmsetupContainer> backing_container =
        std::make_unique<DmsetupContainer>(
            StorageContainerType::kDmDefaultKey, config_.dmsetup_config,
            std::move(backing_device), key_reference, &platform_, nullptr,
            std::make_unique<brillo::DeviceMapper>(
                base::BindRepeating(&brillo::fake::CreateDevmapperTask)));
    ext4_container_ = std::make_unique<Ext4Container>(
        config_.filesystem_config, std::move(backing_container), &platform_,
        nullptr);
  }

 protected:
  base::FilePath partition_file_{"/dev/mmcp10"};
  std::string dmsetup_device_name_{"encrypted"};
  base::FilePath encrypted_device_ =
      base::FilePath("/dev/mapper").Append(dmsetup_device_name_);
  libstorage::StorageContainerConfig config_{
      .filesystem_config = {.tune2fs_opts = {"-Q", "project"},
                            .backend_type = StorageContainerType::kUnencrypted,
                            .recovery = RecoveryType::kDoNothing},
      .dmsetup_config = {.backing_device_config =
                             {.type = libstorage::BackingDeviceType::kPartition,
                              .name = partition_file_.value()},
                         .dmsetup_device_name = dmsetup_device_name_,
                         .dmsetup_cipher = std::string("aes-xts-plain64")}};
  MockPlatform platform_;
  std::unique_ptr<Ext4Container> ext4_container_;
};

// Tests the normal path.
TEST_F(Ext4ContainerDmTest, GetBackingLocation) {
  EXPECT_EQ(ext4_container_->GetBackingLocation(), encrypted_device_);
}

TEST_F(Ext4ContainerDmTest, SetupCreateCheck) {
  EXPECT_CALL(platform_, GetBlkSize(partition_file_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  EXPECT_CALL(platform_, FormatExt4(encrypted_device_, _, _)).Times(0);
  int fsck_err = 0;
  EXPECT_CALL(platform_, Fsck(encrypted_device_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(fsck_err), Return(false)));
  EXPECT_CALL(platform_, Tune2Fs(encrypted_device_, _)).WillOnce(Return(true));

  // Make sure we can read the superblock.
  ASSERT_TRUE(
      platform_.WriteStringToFile(encrypted_device_, std::string(2048, 0)));

  FileSystemKey key;
  EXPECT_TRUE(ext4_container_->Setup(key));
}

TEST_F(Ext4ContainerDmTest, SetupNoSuperblock) {
  EXPECT_CALL(platform_, GetBlkSize(partition_file_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  EXPECT_CALL(platform_, FormatExt4(encrypted_device_, _, _)).Times(0);
  int fsck_err = 0;
  EXPECT_CALL(platform_, Fsck(encrypted_device_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(fsck_err), Return(false)));
  EXPECT_CALL(platform_, Tune2Fs(encrypted_device_, _)).WillOnce(Return(true));

  FileSystemKey key;

  // superblock is too small, will fail.
  EXPECT_FALSE(ext4_container_->Setup(key));
}

}  // namespace libstorage
