// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/cryptohome.h"

#include <algorithm>
#include <numeric>
#include <string>

#include <base/files/file_util.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "brillo/fake_cryptohome.h"

namespace brillo::cryptohome::home {
namespace {

TEST(Cryptohome, GetUserPath) {
  const Username kUsername("fakeuser");
  FakeSystemSaltLoader fake_salt("01234567890123456789");

  EXPECT_EQ(GetUserPath(kUsername).value(),
            "/home/user/856b54169cd5d2d6ca9a4b258ada5e3bee242829");
  EXPECT_EQ(GetUserPath(SanitizeUserName(kUsername)).value(),
            "/home/user/856b54169cd5d2d6ca9a4b258ada5e3bee242829");
}

TEST(Cryptohome, GetRootPath) {
  const Username kUsername("fakeuser");
  FakeSystemSaltLoader fake_salt("01234567890123456789");

  EXPECT_EQ(GetRootPath(kUsername).value(),
            "/home/root/856b54169cd5d2d6ca9a4b258ada5e3bee242829");
  EXPECT_EQ(GetRootPath(SanitizeUserName(kUsername)).value(),
            "/home/root/856b54169cd5d2d6ca9a4b258ada5e3bee242829");
}

TEST(Cryptohome, GetDaemonStorePath) {
  const Username kUsername("fakeuser");
  FakeSystemSaltLoader fake_salt("01234567890123456789");

  EXPECT_EQ(
      GetDaemonStorePath(kUsername, "mydaemon").value(),
      "/run/daemon-store/mydaemon/856b54169cd5d2d6ca9a4b258ada5e3bee242829");
  EXPECT_EQ(
      GetDaemonStorePath(SanitizeUserName(kUsername), "mydaemon").value(),
      "/run/daemon-store/mydaemon/856b54169cd5d2d6ca9a4b258ada5e3bee242829");
}

TEST(Cryptohome, SanitizeUsername) {
  const Username kUsername("fakeuser");
  FakeSystemSaltLoader fake_salt("01234567890123456789");

  const ObfuscatedUsername kExpected(
      "856b54169cd5d2d6ca9a4b258ada5e3bee242829");
  EXPECT_EQ(SanitizeUserName(kUsername), kExpected);
}

TEST(Cryptohome, SanitizeUsernameWithSalt) {
  Username username("fakeuser");
  SecureBlob salt = SecureBlob("01234567890123456789");

  const ObfuscatedUsername kExpected(
      "856b54169cd5d2d6ca9a4b258ada5e3bee242829");
  EXPECT_EQ(kExpected, SanitizeUserNameWithSalt(username, salt));
}

TEST(Cryptohome, SanitizeUsernameWithSaltMixedCase) {
  Username username("fakeuser");
  SecureBlob salt = SecureBlob("01234567890123456789");

  const ObfuscatedUsername kExpected(
      "856b54169cd5d2d6ca9a4b258ada5e3bee242829");
  EXPECT_EQ(kExpected, SanitizeUserNameWithSalt(username, salt));
}

TEST(Cryptohome, FakeSystemSaltLoader) {
  constexpr char kSalt[] = "some-salt";
  FakeSystemSaltLoader fake_salt(kSalt);

  EXPECT_EQ(SystemSaltLoader::GetInstance(), &fake_salt);
}

TEST(Cryptohome, FakeSystemSaltLoaderString) {
  constexpr char kSalt[] = "some-salt";
  FakeSystemSaltLoader fake_salt(kSalt);

  EXPECT_EQ(fake_salt.value(), kSalt);
  EXPECT_TRUE(fake_salt.EnsureLoaded());
  EXPECT_EQ(fake_salt.value(), kSalt);
}

TEST(Cryptohome, FakeSystemSaltLoaderPath) {
  constexpr char kSalt[] = "some-salt";
  base::FilePath file_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path));
  FakeSystemSaltLoader fake_salt(file_path);
  EXPECT_TRUE(base::WriteFile(file_path, kSalt));

  EXPECT_TRUE(fake_salt.EnsureLoaded());
  EXPECT_EQ(fake_salt.value(), kSalt);
}

}  // namespace
}  // namespace brillo::cryptohome::home
