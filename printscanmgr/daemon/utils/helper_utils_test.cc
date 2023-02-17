// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printscanmgr/daemon/utils/helper_utils.h"

#include <limits.h>

#include <gtest/gtest.h>

namespace printscanmgr {

TEST(SandboxedProcessTest, GetHelperPath) {
  std::string full_path;

  EXPECT_TRUE(GetHelperPath("", &full_path));
  EXPECT_EQ("/usr/libexec/debugd/helpers/", full_path);

  EXPECT_TRUE(GetHelperPath("test/me", &full_path));
  EXPECT_EQ("/usr/libexec/debugd/helpers/test/me", full_path);

  std::string str(PATH_MAX, 'x');
  EXPECT_FALSE(GetHelperPath(str, &full_path));
}

}  // namespace printscanmgr
