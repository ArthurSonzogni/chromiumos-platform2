// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/file.h"

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace diagnostics {
namespace {
using ::testing::AllOf;
using ::testing::Ge;
using ::testing::Le;

TEST(FileUtilsDirectTest, GetFileCreationTime) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const auto dir_path = temp_dir.GetPath();
  ASSERT_TRUE(base::DirectoryExists(dir_path));

  base::FilePath file_path;
  const auto before_time = base::Time::NowFromSystemTime();
  ASSERT_TRUE(base::CreateTemporaryFileInDir(dir_path, &file_path));
  const auto after_time = base::Time::NowFromSystemTime();

  base::Time creation_time;
  ASSERT_TRUE(GetCreationTime(file_path, creation_time));
  // Because the conversion from statx_timestamp to base::Time involves floating
  // point roundoff, we give 1 second breathing space. The main point is to
  // check the file creation time obtained is reasonable. After all, an accurate
  // check would require rewriting the logic to get creation time in the test.
  EXPECT_THAT(creation_time, AllOf(Ge(before_time - base::Seconds(1)),
                                   Le(after_time + base::Seconds(1))));
}
}  // namespace
}  // namespace diagnostics
