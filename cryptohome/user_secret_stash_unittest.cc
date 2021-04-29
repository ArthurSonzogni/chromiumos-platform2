// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "cryptohome/cryptolib.h"

namespace cryptohome {

namespace {

static bool FindBlobInBlob(const brillo::SecureBlob& haystack,
                           const brillo::SecureBlob& needle) {
  return std::search(haystack.begin(), haystack.end(), needle.begin(),
                     needle.end()) != haystack.end();
}

}  // namespace

TEST(UserSecretStashTest, InitializeRandomTest) {
  UserSecretStash stash;
  stash.InitializeRandom();
  EXPECT_FALSE(stash.GetFileSystemKey().empty());
  EXPECT_FALSE(stash.GetResetSecret().empty());
}

TEST(UserSecretStashTest, GetEncryptedUSS) {
  UserSecretStash stash;
  stash.InitializeRandom();

  brillo::SecureBlob main_key(kAesGcm256KeySize);
  memset(main_key.data(), 0xA, main_key.size());

  auto wrapped_uss = stash.GetAesGcmEncrypted(main_key);
  EXPECT_NE(base::nullopt, wrapped_uss);

  // No raw secrets in the encrypted USS, which is written to disk.
  brillo::SecureBlob wrapped_blob(wrapped_uss->begin(), wrapped_uss->end());
  EXPECT_FALSE(FindBlobInBlob(wrapped_blob, stash.GetFileSystemKey()));
  EXPECT_FALSE(FindBlobInBlob(wrapped_blob, stash.GetResetSecret()));
}

}  // namespace cryptohome
