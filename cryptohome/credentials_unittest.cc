// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Credentials.

#include "cryptohome/credentials.h"

#include <string.h>  // For memset(), memcpy(), memcmp().

#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <string>

#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;

namespace cryptohome {

const char kFakeUser[] = "fakeuser";
const char kFakePasskey[] = "176c1e698b521373d77ce655d2e56a1d";

// salt must have even number of characters.
const char kFakeSystemSalt[] = "01234567890123456789";

TEST(CredentialsTest, UsernameTest) {
  char username[80];
  snprintf(username, sizeof(username), "%s%s", kFakeUser, "@gmail.com");
  Credentials credentials(Username{username}, SecureBlob(kFakePasskey));
  EXPECT_EQ(username, *credentials.username());
}

TEST(CredentialsTest, GetObfuscatedUsernameTest) {
  Credentials credentials(Username{kFakeUser}, SecureBlob(kFakePasskey));
  MockPlatform platform;

  brillo::SecureBlob fake_salt;
  EXPECT_TRUE(
      brillo::SecureBlob::HexStringToSecureBlob(kFakeSystemSalt, &fake_salt));
  platform.GetFake()->SetSystemSaltForLibbrillo(fake_salt);

  EXPECT_EQ("bb0ae3fcd181eefb861b4f0ee147a316e51d9f04",
            *credentials.GetObfuscatedUsername());

  platform.GetFake()->RemoveSystemSaltForLibbrillo();
}

TEST(CredentialsTest, PasskeyTest) {
  Credentials credentials(Username{kFakeUser}, SecureBlob(kFakePasskey));
  const SecureBlob passkey = credentials.passkey();
  EXPECT_EQ(strlen(kFakePasskey), passkey.size());
  EXPECT_EQ(0, memcmp(kFakePasskey, passkey.data(), passkey.size()));
}

}  // namespace cryptohome
