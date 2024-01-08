// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/path_utils.h"

#include <base/files/file_path.h>
#include <gtest/gtest.h>

namespace debugd {

TEST(PathUtilsTest, GetFilePath) {
  EXPECT_EQ("/sys/foo", path_utils::GetFilePath("/sys/foo").value());
}

TEST(PathUtilsTest, SetPrefixForTesting) {
  path_utils::testing::SetPrefixForTesting(base::FilePath("/tmp"));
  EXPECT_EQ("/tmp/sys/foo", path_utils::GetFilePath("/sys/foo").value());
  path_utils::testing::SetPrefixForTesting(base::FilePath());
  EXPECT_EQ("/sys/foo", path_utils::GetFilePath("/sys/foo").value());
}

}  // namespace debugd
