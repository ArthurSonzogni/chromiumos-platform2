// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/filesystem_layout.h"

#include <base/files/file_path.h>
#include <gtest/gtest.h>

namespace cryptohome {

namespace {

constexpr char kObfuscatedUsername[] = "fake-user";

}  // namespace

TEST(FileSystemLayoutTest, UserSecretStashPath) {
  EXPECT_EQ(UserSecretStashPath(kObfuscatedUsername, /*slot=*/0),
            base::FilePath("/home/.shadow/fake-user/user_secret_stash/uss.0"));
  EXPECT_EQ(
      UserSecretStashPath(kObfuscatedUsername, /*slot=*/123),
      base::FilePath("/home/.shadow/fake-user/user_secret_stash/uss.123"));
}

}  // namespace cryptohome
