// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "libstorage/storage_container/storage_container_factory.h"

#include <linux/magic.h>
#include <sys/statfs.h>

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/mock_platform.h>

namespace libstorage {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

namespace {
constexpr char kBackingBase[] = "/mytmpfs";
constexpr char kBackingFile[] = "invalid";
constexpr int kEphemeralfsFragmentSize = 1 << 10;
const int kEphemeralfsSize = 1 << 12;

}  // namespace

class StorageContainerFactoryTest : public ::testing::Test {
 public:
  StorageContainerFactoryTest() {}

  void SetUp() override {
    std::unique_ptr<BackingDeviceFactory> backing_device_factory =
        std::make_unique<BackingDeviceFactory>(&platform_);
    storage_container_factory_ = std::make_unique<StorageContainerFactory>(
        &platform_, nullptr, std::unique_ptr<Keyring>(),
        std::move(backing_device_factory));
  }

 protected:
  StrictMock<MockPlatform> platform_;
  std::unique_ptr<StorageContainerFactory> storage_container_factory_;
};

namespace {

TEST_F(StorageContainerFactoryTest, InvalidEphemeralValidUnencrypted) {
  // Create an ephemeral device not backed by a ramdisk.
  libstorage::StorageContainerConfig config;
  config.unencrypted_config = {
      .backing_device_config = {
          .type = libstorage::BackingDeviceType::kLoopbackDevice,
          .loopback = {.backing_file_path =
                           base::FilePath(kBackingBase).Append(kBackingFile)}}};

  // We should fail before reaching the backend factory and invoking any
  // platform function.
  auto invalid_ephemeral = storage_container_factory_->Generate(
      config, libstorage::StorageContainerType::kEphemeral,
      libstorage::FileSystemKeyReference());
  EXPECT_FALSE(invalid_ephemeral);

  // But the configuration is fine for unencrypted.
  auto valid_unencrypted = storage_container_factory_->Generate(
      config, libstorage::StorageContainerType::kUnencrypted,
      libstorage::FileSystemKeyReference());
  EXPECT_TRUE(valid_unencrypted);
  EXPECT_EQ(valid_unencrypted->GetType(),
            libstorage::StorageContainerType::kUnencrypted);
}

TEST_F(StorageContainerFactoryTest, ValidEphemeral) {
  // Create an ephemeral device backed by a ramdisk.
  libstorage::StorageContainerConfig config;
  config.unencrypted_config = {
      .backing_device_config = {
          .type = libstorage::BackingDeviceType::kRamdiskDevice,
          .ramdisk = {.backing_file_path =
                          base::FilePath(kBackingBase).Append(kBackingFile)}}};

  // Report a valid statfs to complete Generate() call.
  struct statfs ephemeral_statfs = {
      .f_type = TMPFS_MAGIC,
      .f_blocks = kEphemeralfsSize / kEphemeralfsFragmentSize,
      .f_frsize = kEphemeralfsFragmentSize,
  };

  EXPECT_CALL(platform_, StatFS(base::FilePath("/"), _))
      .WillOnce(DoAll(SetArgPointee<1>(ephemeral_statfs), Return(true)));

  auto valid_ephemeral = storage_container_factory_->Generate(
      config, libstorage::StorageContainerType::kEphemeral,
      libstorage::FileSystemKeyReference());
  EXPECT_TRUE(valid_ephemeral);
  EXPECT_EQ(valid_ephemeral->GetType(),
            libstorage::StorageContainerType::kEphemeral);
  // When tearing down the container, we will try to delete the ramdisk file in
  // teardown and purge.
  EXPECT_CALL(platform_, GetLoopDeviceManager());
  EXPECT_CALL(platform_,
              DeleteFile(base::FilePath(kBackingBase).Append(kBackingFile)))
      .Times(2);
  EXPECT_CALL(platform_, DeleteFileDurable(
                             base::FilePath(kBackingBase).Append(kBackingFile)))
      .Times(3);
}

}  // namespace

}  // namespace libstorage
