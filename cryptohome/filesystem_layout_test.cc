// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/filesystem_layout.h"

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libstorage/platform/mock_platform.h>

#include "cryptohome/cryptohome_common.h"

namespace cryptohome {

using hwsec_foundation::CreateSecureRandomBlob;
using testing::NiceMock;

TEST(FileSystemLayoutTest, UserSecretStashPath) {
  const ObfuscatedUsername kObfuscatedUsername("fake-user");

  EXPECT_EQ(UserSecretStashPath(kObfuscatedUsername, /*slot=*/0),
            base::FilePath("/home/.shadow/fake-user/user_secret_stash/uss.0"));
  EXPECT_EQ(
      UserSecretStashPath(kObfuscatedUsername,
                          /*slot=*/123),
      base::FilePath("/home/.shadow/fake-user/user_secret_stash/uss.123"));
}

TEST(FileSystemLayout, CreateNewSalt) {
  NiceMock<libstorage::MockPlatform> platform;
  brillo::SecureBlob salt;
  brillo::SecureBlob salt2;

  // Fake platform initializer call into layout initialization.
  // Clean the relevant state up.
  std::ignore = platform.DeleteFile(LegacySystemSaltFile());
  std::ignore = platform.DeleteFile(SystemSaltFile());

  ASSERT_FALSE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_FALSE(platform.FileExists(SystemSaltFile()));
  ASSERT_TRUE(GetSystemSalt(&platform, &salt));
  ASSERT_FALSE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_TRUE(platform.FileExists(SystemSaltFile()));
  ASSERT_TRUE(GetSystemSalt(&platform, &salt2));
  ASSERT_FALSE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_TRUE(platform.FileExists(SystemSaltFile()));
  ASSERT_EQ(salt.to_string(), salt2.to_string());
}

TEST(FileSystemLayout, LegacySaltExists) {
  NiceMock<libstorage::MockPlatform> platform;

  brillo::SecureBlob salt;
  brillo::SecureBlob local_salt =
      CreateSecureRandomBlob(kCryptohomeDefaultSaltLength);

  // Fake platform initializer call into layout initialization.
  // Clean the relevant state up.
  std::ignore = platform.DeleteFile(LegacySystemSaltFile());
  std::ignore = platform.DeleteFile(SystemSaltFile());

  ASSERT_TRUE(platform.WriteSecureBlobToFileAtomicDurable(
      LegacySystemSaltFile(), local_salt, 0644));
  ASSERT_TRUE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_FALSE(platform.FileExists(SystemSaltFile()));
  ASSERT_TRUE(GetSystemSalt(&platform, &salt));
  ASSERT_TRUE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_FALSE(platform.FileExists(SystemSaltFile()));
  ASSERT_EQ(salt.to_string(), local_salt.to_string());
  ASSERT_TRUE(GetSystemSalt(&platform, &salt));
  ASSERT_TRUE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_FALSE(platform.FileExists(SystemSaltFile()));
  ASSERT_EQ(salt.to_string(), local_salt.to_string());
}

TEST(FileSystemLayout, LegacySaltPreferred) {
  NiceMock<libstorage::MockPlatform> platform;

  brillo::SecureBlob salt;
  brillo::SecureBlob legacy_salt =
      CreateSecureRandomBlob(kCryptohomeDefaultSaltLength);
  brillo::SecureBlob new_salt =
      CreateSecureRandomBlob(kCryptohomeDefaultSaltLength);

  // Fake platform initializer call into layout initialization.
  // Clean the relevant state up.
  std::ignore = platform.DeleteFile(LegacySystemSaltFile());
  std::ignore = platform.DeleteFile(SystemSaltFile());

  ASSERT_TRUE(platform.WriteSecureBlobToFileAtomicDurable(
      LegacySystemSaltFile(), legacy_salt, 0644));
  ASSERT_TRUE(platform.WriteSecureBlobToFileAtomicDurable(SystemSaltFile(),
                                                          new_salt, 0644));
  ASSERT_TRUE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_TRUE(platform.FileExists(SystemSaltFile()));
  ASSERT_TRUE(GetSystemSalt(&platform, &salt));
  ASSERT_TRUE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_TRUE(platform.FileExists(SystemSaltFile()));
  ASSERT_EQ(salt.to_string(), legacy_salt.to_string());
  ASSERT_TRUE(GetSystemSalt(&platform, &salt));
  ASSERT_TRUE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_TRUE(platform.FileExists(SystemSaltFile()));
  ASSERT_EQ(salt.to_string(), legacy_salt.to_string());
}

TEST(FileSystemLayout, LegacySaltTooLarge) {
  NiceMock<libstorage::MockPlatform> platform;

  // It should be an error if a salt is this large (2 MiB).
  static constexpr int64_t kTooLargeSaltSize = 2 * (1 << 20);

  brillo::SecureBlob salt1, salt2;
  brillo::SecureBlob big_salt = CreateSecureRandomBlob(kTooLargeSaltSize);

  // Fake platform initializer call into layout initialization.
  // Clean the relevant state up.
  std::ignore = platform.DeleteFile(LegacySystemSaltFile());
  std::ignore = platform.DeleteFile(SystemSaltFile());

  ASSERT_TRUE(platform.WriteSecureBlobToFileAtomicDurable(
      LegacySystemSaltFile(), big_salt, 0644));
  ASSERT_TRUE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_FALSE(platform.FileExists(SystemSaltFile()));
  ASSERT_TRUE(GetSystemSalt(&platform, &salt1));
  ASSERT_FALSE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_TRUE(platform.FileExists(SystemSaltFile()));
  ASSERT_TRUE(GetSystemSalt(&platform, &salt2));
  ASSERT_FALSE(platform.FileExists(LegacySystemSaltFile()));
  ASSERT_TRUE(platform.FileExists(SystemSaltFile()));
  ASSERT_EQ(salt1, salt2);
  ASSERT_NE(salt1, big_salt);
}

}  // namespace cryptohome
