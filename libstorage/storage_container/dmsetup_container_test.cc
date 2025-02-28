// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/dmsetup_container.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/keyring/fake_keyring.h>
#include <libstorage/platform/keyring/utils.h>
#include <libstorage/platform/mock_platform.h>

#include "libstorage/storage_container/ext4_container.h"
#include "libstorage/storage_container/fake_backing_device.h"
#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/partition_device.h"
#include "libstorage/storage_container/storage_container.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace libstorage {

class DmsetupContainerTest : public ::testing::Test {
 public:
  DmsetupContainerTest()
      : config_({.dmsetup_device_name = "crypt_device",
                 .dmsetup_cipher = "aes-xts-plain64"}),
        key_({.fek = brillo::SecureBlob("random key")}),
        key_reference_({.fek_sig = brillo::SecureBlob("random reference")}),
        device_mapper_(base::BindRepeating(&brillo::fake::CreateDevmapperTask)),
        backing_device_(std::make_unique<FakeBackingDevice>(
            BackingDeviceType::kLogicalVolumeBackingDevice,
            base::FilePath("/dev/VG/LV"))) {
    auto keyring_key_reference =
        dmcrypt::GenerateKeyringDescription(key_reference_.fek_sig);
    key_descriptor_ = dmcrypt::GenerateDmcryptKeyDescriptor(
        keyring_key_reference.fek_sig, key_.fek.size());
  }
  ~DmsetupContainerTest() override = default;

  void GenerateContainer() {
    container_ = std::make_unique<DmsetupContainer>(
        StorageContainerType::kDmcrypt, config_, std::move(backing_device_),
        key_reference_, &platform_, &keyring_,
        std::make_unique<brillo::DeviceMapper>(
            base::BindRepeating(&brillo::fake::CreateDevmapperTask)));
  }

 protected:
  DmsetupConfig config_;

  FileSystemKey key_;
  FileSystemKeyReference key_reference_;
  MockPlatform platform_;
  FakeKeyring keyring_;
  brillo::DeviceMapper device_mapper_;
  std::unique_ptr<BackingDevice> backing_device_;
  std::unique_ptr<DmsetupContainer> container_;
  brillo::SecureBlob key_descriptor_;
};

// Tests the creation path for the dm-crypt container.
TEST_F(DmsetupContainerTest, SetupCreateCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  // Check that the device mapper target exists.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmsetup_device_name).CryptGetKey(),
            key_descriptor_);
  EXPECT_TRUE(device_mapper_.Remove(config_.dmsetup_device_name));
}

// Tests the setup path with an existing container.
TEST_F(DmsetupContainerTest, SetupNoCreateCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  // Check that the device mapper target exists.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmsetup_device_name).CryptGetKey(),
            key_descriptor_);
  EXPECT_TRUE(device_mapper_.Remove(config_.dmsetup_device_name));
}

// Tests that teardown doesn't leave an active dm-crypt device or an attached
// backing device.
TEST_F(DmsetupContainerTest, TeardownCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  // Now, attempt teardown of the device.
  EXPECT_TRUE(container_->Teardown());
  // Check that the device mapper target doesn't exist.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmsetup_device_name).CryptGetKey(),
            brillo::SecureBlob());
}

// Tests that EvictKey doesn't leave an active dm-crypt device.
TEST_F(DmsetupContainerTest, EvictKeyCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  EXPECT_TRUE(container_->EvictKey());

  // Check that the key in memory has been zeroed from the table
  EXPECT_FALSE(container_->IsDeviceKeyValid());

  // Do the eviction again, should return true and no-op.
  EXPECT_TRUE(container_->EvictKey());

  // Now, attempt teardown of the device.
  EXPECT_FALSE(container_->Teardown());

  // Device mapper target still exists, but remapping to error allows
  // the device to be force unmounted later on for shutdown purposes.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmsetup_device_name).GetType(),
            "error");
}

// Tests that the dmcrypt container can be reset.
TEST_F(DmsetupContainerTest, ResetRawDeviceContainerTest) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_CALL(platform_,
              DiscardDevice(base::FilePath("/dev/mapper/crypt_device")))
      .WillOnce(Return(true));

  EXPECT_TRUE(container_->Setup(key_));
  // Attempt a reset of the device.
  EXPECT_TRUE(container_->Reset());
  EXPECT_TRUE(container_->Teardown());
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
