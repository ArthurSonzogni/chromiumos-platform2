// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/ecryptfs_container.h"

#include <memory>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/keyring/fake_keyring.h>
#include <libstorage/platform/mock_platform.h>

#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/storage_container.h"

using ::testing::_;
using ::testing::Return;

namespace libstorage {

class EcryptfsContainerTest : public ::testing::Test {
 public:
  EcryptfsContainerTest()
      : backing_dir_(base::FilePath("/a/b/c")),
        key_reference_({.fek_sig = brillo::SecureBlob("random_keysig"),
                        .fnek_sig = brillo::SecureBlob("random_fnek_sig")}),
        key_({.fek = brillo::SecureBlob("random key"),
              .fnek = brillo::SecureBlob("random_fnek"),
              .fek_salt = brillo::SecureBlob("random_fek_salt"),
              .fnek_salt = brillo::SecureBlob("random_fnek_salt")}),
        container_(std::make_unique<EcryptfsContainer>(
            backing_dir_, key_reference_, &platform_, &keyring_)) {}
  ~EcryptfsContainerTest() override = default;

 protected:
  base::FilePath backing_dir_;
  FileSystemKeyReference key_reference_;
  FileSystemKey key_;
  MockPlatform platform_;
  FakeKeyring keyring_;
  std::unique_ptr<StorageContainer> container_;
};

// Tests the creation path for an eCryptFs container.
TEST_F(EcryptfsContainerTest, SetupCreateCheck) {
  EXPECT_TRUE(container_->Setup(key_));
  EXPECT_TRUE(platform_.DirectoryExists(backing_dir_));
  EXPECT_TRUE(keyring_.HasKey(Keyring::KeyType::kEcryptfsKey, key_reference_));
}

// Tests the setup path for an existing eCryptFs container.
TEST_F(EcryptfsContainerTest, SetupNoCreateCheck) {
  EXPECT_TRUE(platform_.CreateDirectory(backing_dir_));
  EXPECT_TRUE(container_->Setup(key_));
  EXPECT_TRUE(keyring_.HasKey(Keyring::KeyType::kEcryptfsKey, key_reference_));
}

// Tests the failure path on failing to add the eCryptFs auth token to the
// user keyring.
TEST_F(EcryptfsContainerTest, SetupFailedEncryptionKeyAdd) {
  keyring_.SetShouldFail(true);
  EXPECT_FALSE(container_->Setup(key_));
  EXPECT_FALSE(keyring_.HasKey(Keyring::KeyType::kEcryptfsKey, key_reference_));
}

// Tests the teardown invalidates the key.
TEST_F(EcryptfsContainerTest, TeardownInvalidateKey) {
  EXPECT_TRUE(container_->Setup(key_));
  EXPECT_TRUE(keyring_.HasKey(Keyring::KeyType::kEcryptfsKey, key_reference_));
  EXPECT_TRUE(container_->Teardown());
  EXPECT_FALSE(keyring_.HasKey(Keyring::KeyType::kEcryptfsKey, key_reference_));
}

}  // namespace libstorage
