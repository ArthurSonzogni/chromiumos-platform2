// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/debug_log.h"

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lorgnette/test_util.h"

using ::testing::IsNull;
using ::testing::NotNull;

namespace lorgnette {

class DebugLogTest : public testing::Test {
 protected:
  void SetUp() {
    unsetenv("PFUFS_DEBUG");
    unsetenv("SANE_DEBUG_AIRSCAN");
    unsetenv("SANE_DEBUG_EPSONDS");
    unsetenv("SANE_DEBUG_EPSON2");
    unsetenv("SANE_DEBUG_FUJITSU");
    unsetenv("SANE_DEBUG_PIXMA");
  }
};

TEST_F(DebugLogTest, CreateEnvWhenFlagExists) {
  base::FilePath path;
  ASSERT_TRUE(base::CreateTemporaryFile(&path));

  EXPECT_TRUE(SetupDebugging(path));

  EXPECT_THAT(getenv("PFUFS_DEBUG"), NotNull());
  EXPECT_THAT(getenv("SANE_DEBUG_AIRSCAN"), NotNull());
  EXPECT_THAT(getenv("SANE_DEBUG_EPSONDS"), NotNull());
  EXPECT_THAT(getenv("SANE_DEBUG_EPSON2"), NotNull());
  EXPECT_THAT(getenv("SANE_DEBUG_FUJITSU"), NotNull());
  EXPECT_THAT(getenv("SANE_DEBUG_PIXMA"), NotNull());
}

TEST_F(DebugLogTest, DontCreateEnvWhenFlagMissing) {
  base::FilePath path("/no/such/file");

  EXPECT_FALSE(SetupDebugging(path));

  EXPECT_THAT(getenv("PFUFS_DEBUG"), IsNull());
  EXPECT_THAT(getenv("SANE_DEBUG_AIRSCAN"), IsNull());
  EXPECT_THAT(getenv("SANE_DEBUG_EPSONDS"), IsNull());
  EXPECT_THAT(getenv("SANE_DEBUG_EPSON2"), IsNull());
  EXPECT_THAT(getenv("SANE_DEBUG_FUJITSU"), IsNull());
  EXPECT_THAT(getenv("SANE_DEBUG_PIXMA"), IsNull());
}

}  // namespace lorgnette
