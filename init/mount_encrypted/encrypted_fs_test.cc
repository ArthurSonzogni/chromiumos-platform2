// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/mount_encrypted/encrypted_fs.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/keyring/fake_keyring.h>
#include <libstorage/platform/keyring/utils.h>
#include <libstorage/platform/mock_platform.h>
#include <libstorage/storage_container/backing_device.h>
#include <libstorage/storage_container/dmcrypt_container.h>
#include <libstorage/storage_container/ext4_container.h>
#include <libstorage/storage_container/fake_backing_device.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace mount_encrypted {

class EncryptedFsTest : public ::testing::Test {
 public:
  EncryptedFsTest()
      : dmcrypt_name_("encstateful"),
        dmcrypt_device_(base::FilePath("/dev/mapper/encstateful")),
        config_(
            {.filesystem_config =
                 {
                     .mkfs_opts = {"-O", "encrypt,verity"},
                     .tune2fs_opts = {"-Q", "project"},
                     .backend_type = libstorage::StorageContainerType::kDmcrypt,
                     .recovery = libstorage::RecoveryType::kEnforceCleaning,
                 },
             .dmcrypt_config =
                 {.backing_device_config =
                      {.type = libstorage::BackingDeviceType::kLoopbackDevice,
                       .name = "encstateful"},
                  .dmcrypt_device_name = dmcrypt_name_,
                  .dmcrypt_cipher = "aes-cbc-essiv:sha256"}}),
        device_mapper_(base::BindRepeating(&brillo::fake::CreateDevmapperTask)),
        fake_backing_device_factory_(&platform_) {
    // Set up a fake backing device.
    auto fake_backing_device = fake_backing_device_factory_.Generate(
        config_.dmcrypt_config.backing_device_config);
    backing_device_ = fake_backing_device.get();

    // Set encryption key.
    brillo::SecureBlob secret;
    brillo::SecureBlob::HexStringToSecureBlob("0123456789ABCDEF", &secret);
    key_.fek = secret;
    key_reference_ = {.fek_sig = brillo::SecureBlob("some_ref")};
    auto keyring_key_reference =
        libstorage::dmcrypt::GenerateKeyringDescription(key_reference_.fek_sig);
    key_descriptor_ = libstorage::dmcrypt::GenerateDmcryptKeyDescriptor(
        keyring_key_reference.fek_sig, key_.fek.size());

    auto dmcrypt_container = std::make_unique<libstorage::DmcryptContainer>(
        config_.dmcrypt_config, std::move(fake_backing_device), key_reference_,
        &platform_, &keyring_,
        std::make_unique<brillo::DeviceMapper>(
            base::BindRepeating(&brillo::fake::CreateDevmapperTask)));
    auto ext4_container = std::make_unique<libstorage::Ext4Container>(
        config_.filesystem_config, std::move(dmcrypt_container), &platform_,
        /* metrics */ nullptr);

    encrypted_fs_ = std::make_unique<EncryptedFs>(
        rootdir_, statefulmnt_, 3UL * 1024 * 1024 * 1024, dmcrypt_name_,
        std::move(ext4_container), &platform_);
  }
  ~EncryptedFsTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(platform_.CreateDirectory(statefulmnt_));
    ASSERT_TRUE(platform_.CreateDirectory(rootdir_.Append("var")));
    ASSERT_TRUE(
        platform_.CreateDirectory(rootdir_.Append("home").Append("chronos")));
  }

  void ExpectSetup() {
    EXPECT_CALL(platform_, GetBlkSize(_, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(40920000), Return(true)));
    EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));
    EXPECT_CALL(platform_, Tune2Fs(dmcrypt_device_, _)).WillOnce(Return(true));
    EXPECT_CALL(platform_, ResizeFilesystem(dmcrypt_device_,
                                            9990 /* blocks of 4096 bytes */))
        .WillOnce(Return(true));
    ASSERT_TRUE(
        platform_.WriteStringToFile(dmcrypt_device_, std::string(2048, 0)));
  }

  void ExpectCheck() {
    EXPECT_CALL(platform_, Fsck(dmcrypt_device_, _, _)).WillOnce(Return(true));
  }

  void ExpectCreate() {
    EXPECT_CALL(platform_, FormatExt4(dmcrypt_device_, _, _))
        .WillOnce(Return(true));
  }

 protected:
  const std::string dmcrypt_name_;
  const base::FilePath dmcrypt_device_;
  libstorage::StorageContainerConfig config_;

  NiceMock<libstorage::MockPlatform> platform_;
  base::FilePath rootdir_{"/"};
  base::FilePath statefulmnt_{"/stateful_test"};
  libstorage::FakeKeyring keyring_;
  brillo::DeviceMapper device_mapper_;
  libstorage::FakeBackingDeviceFactory fake_backing_device_factory_;
  libstorage::FileSystemKey key_;
  libstorage::FileSystemKeyReference key_reference_;
  brillo::SecureBlob key_descriptor_;
  libstorage::BackingDevice* backing_device_;
  std::unique_ptr<EncryptedFs> encrypted_fs_;
};

TEST_F(EncryptedFsTest, RebuildStateful) {
  ExpectSetup();
  ExpectCreate();

  // Check if dm device is mounted and has the correct key.
  EXPECT_TRUE(encrypted_fs_->Setup(key_, true));

  // Check that the dm-crypt device is created and has the correct key.
  brillo::DevmapperTable table = device_mapper_.GetTable(dmcrypt_name_);
  EXPECT_EQ(table.CryptGetKey(), key_descriptor_);
  // Check if backing device is attached.
  EXPECT_EQ(backing_device_->GetPath(), base::FilePath("/dev/encstateful"));

  EXPECT_TRUE(encrypted_fs_->Teardown());

  // Make sure no devmapper device is left.
  EXPECT_EQ(device_mapper_.GetTable(dmcrypt_name_).CryptGetKey(),
            brillo::SecureBlob());
  // Check if backing device is not attached.
  EXPECT_EQ(backing_device_->GetPath(), std::nullopt);
}

TEST_F(EncryptedFsTest, OldStateful) {
  ExpectSetup();
  ExpectCheck();

  // Create the fake backing device.
  ASSERT_TRUE(backing_device_->Create());

  // Expect setup to succeed.
  EXPECT_TRUE(encrypted_fs_->Setup(key_, false));
  // Check that the dm-crypt device is created and has the correct key.
  brillo::DevmapperTable table = device_mapper_.GetTable(dmcrypt_name_);
  EXPECT_EQ(table.CryptGetKey(), key_descriptor_);
  // Check if backing device is attached.
  EXPECT_EQ(backing_device_->GetPath(), base::FilePath("/dev/encstateful"));

  EXPECT_TRUE(encrypted_fs_->Teardown());
  // Make sure no devmapper device is left.
  EXPECT_EQ(device_mapper_.GetTable(dmcrypt_name_).CryptGetKey(),
            brillo::SecureBlob());
  // Check if backing device is not attached.
  EXPECT_EQ(backing_device_->GetPath(), std::nullopt);
}

TEST_F(EncryptedFsTest, LoopdevTeardown) {
  // BlkSize == 0 --> Teardown loopdev
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), Return(true)));

  // Create the fake backing device.
  ASSERT_TRUE(backing_device_->Create());
  // Expect setup to fail.
  EXPECT_FALSE(encrypted_fs_->Setup(key_, false));
  // Make sure that the backing device is not left attached.
  EXPECT_EQ(backing_device_->GetPath(), std::nullopt);
}

TEST_F(EncryptedFsTest, DevmapperTeardown) {
  // Mount failed --> Teardown devmapper
  ExpectSetup();
  EXPECT_CALL(platform_, Mount(_, _, _, _, _)).WillOnce(Return(false));

  // Create the fake backing device.
  ASSERT_TRUE(backing_device_->Create());
  // Expect setup to fail.
  EXPECT_FALSE(encrypted_fs_->Setup(key_, false));
  // Make sure that the backing device is no left attached.
  EXPECT_EQ(backing_device_->GetPath(), std::nullopt);
}

}  // namespace mount_encrypted
