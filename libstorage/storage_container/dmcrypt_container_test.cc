// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/dmcrypt_container.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>
#include <brillo/secure_blob.h>
#include <libstorage/platform/keyring/fake_keyring.h>
#include <libstorage/platform/keyring/utils.h>
#include <libstorage/platform/mock_platform.h>

#include "libstorage/storage_container/fake_backing_device.h"
#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/storage_container.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace libstorage {

class DmcryptContainerTest : public ::testing::Test {
 public:
  DmcryptContainerTest()
      : config_({.dmcrypt_device_name = "crypt_device",
                 .dmcrypt_cipher = "aes-xts-plain64"}),
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
  ~DmcryptContainerTest() override = default;

  void GenerateContainer() {
    container_ = std::make_unique<DmcryptContainer>(
        config_, std::move(backing_device_), key_reference_, &platform_,
        &keyring_,
        std::make_unique<brillo::DeviceMapper>(
            base::BindRepeating(&brillo::fake::CreateDevmapperTask)));
  }

 protected:
  DmcryptConfig config_;

  FileSystemKey key_;
  FileSystemKeyReference key_reference_;
  MockPlatform platform_;
  FakeKeyring keyring_;
  brillo::DeviceMapper device_mapper_;
  std::unique_ptr<BackingDevice> backing_device_;
  std::unique_ptr<DmcryptContainer> container_;
  brillo::SecureBlob key_descriptor_;
};

// Tests the creation path for the dm-crypt container.
TEST_F(DmcryptContainerTest, SetupCreateCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  // Check that the device mapper target exists.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            key_descriptor_);
  EXPECT_TRUE(device_mapper_.Remove(config_.dmcrypt_device_name));
}

// Tests the setup path with an existing container.
TEST_F(DmcryptContainerTest, SetupNoCreateCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  // Check that the device mapper target exists.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            key_descriptor_);
  EXPECT_TRUE(device_mapper_.Remove(config_.dmcrypt_device_name));
}

// Tests that teardown doesn't leave an active dm-crypt device or an attached
// backing device.
TEST_F(DmcryptContainerTest, TeardownCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  // Now, attempt teardown of the device.
  EXPECT_TRUE(container_->Teardown());
  // Check that the device mapper target doesn't exist.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            brillo::SecureBlob());
}

// Tests that EvictKey doesn't leave an active dm-crypt device.
TEST_F(DmcryptContainerTest, EvictKeyCheck) {
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
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).GetType(),
            "error");
}

// Tests that RestoreKey resume the device with a key.
TEST_F(DmcryptContainerTest, RestoreKeyCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  EXPECT_TRUE(container_->EvictKey());
  // Check that the key in memory has been zeroed from the table.
  EXPECT_FALSE(container_->IsDeviceKeyValid());

  EXPECT_TRUE(container_->RestoreKey(key_));

  // Check that the key in memory has been restored from the table.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            key_descriptor_);
}

// Tests that RestoreKey works with a device that hasn't been evicted.
TEST_F(DmcryptContainerTest, RestoreKeyCheckOnValidTable) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_));
  // Check that the key in memory is valid.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            key_descriptor_);

  EXPECT_TRUE(container_->RestoreKey(key_));

  // Check that the key in memory is still valid.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            key_descriptor_);
}

// Tests that the dmcrypt container can be reset.
TEST_F(DmcryptContainerTest, ResetRawDeviceContainerTest) {
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

}  // namespace libstorage
