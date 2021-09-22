// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash_storage.h"

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;
using testing::_;
using testing::Return;

namespace cryptohome {

namespace {
constexpr char kUssContainer[] = "fake_uss_container";
constexpr char kObfuscatedUsername[] = "foo@gmail.com";
}  // namespace

class UserSecretStashStorageTest : public ::testing::Test {
 protected:
  MockPlatform platform_;
};

// Test the successful scenario of the USS persisting and loading.
TEST_F(UserSecretStashStorageTest, PersistThenLoad) {
  // Write the USS.
  EXPECT_TRUE(PersistUserSecretStash(SecureBlob(kUssContainer),
                                     kObfuscatedUsername, &platform_));
  EXPECT_TRUE(platform_.FileExists(UserSecretStashPath(kObfuscatedUsername)));

  // Load the USS and check it didn't change.
  base::Optional<SecureBlob> loaded_uss_container =
      LoadPersistedUserSecretStash(kObfuscatedUsername, &platform_);
  ASSERT_TRUE(loaded_uss_container);
  EXPECT_EQ(loaded_uss_container->to_string(), kUssContainer);
}

// Test that the persisting fails when the USS file writing fails.
TEST_F(UserSecretStashStorageTest, PersistFailure) {
  EXPECT_CALL(platform_, WriteSecureBlobToFileAtomicDurable(
                             UserSecretStashPath(kObfuscatedUsername), _, _))
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(PersistUserSecretStash(SecureBlob(kUssContainer),
                                      kObfuscatedUsername, &platform_));
}

// Test that the loading fails when the USS file doesn't exist.
TEST_F(UserSecretStashStorageTest, LoadFailureNonExisting) {
  EXPECT_FALSE(LoadPersistedUserSecretStash(kObfuscatedUsername, &platform_));
}

}  // namespace cryptohome
