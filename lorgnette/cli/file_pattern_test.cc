// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/file_pattern.h"

#include <string>
#include <utility>

#include <base/strings/string_util.h>
#include <gtest/gtest.h>

namespace lorgnette::cli {

namespace {

TEST(FilePatternTest, ExpandPatternNoSubs) {
  EXPECT_EQ(ExpandPattern("pattern.tif", 1, "name", "tif"),
            base::FilePath("pattern.tif"));
  EXPECT_EQ(ExpandPattern("pattern.tif", 2, "name", "tif"),
            base::FilePath("pattern_page2.tif"));
}

TEST(FilePatternTest, ExpandPatternDuplicateSubs) {
  EXPECT_EQ(ExpandPattern("%n-%s-%e_pattern_%n-%s-%e.png", 1, "name", "png"),
            base::FilePath("1-name-png_pattern_%n-%s-%e.png"));
  EXPECT_EQ(ExpandPattern("%n-%s-%e_pattern_%n-%s-%e.png", 2, "name", "png"),
            base::FilePath("2-name-png_pattern_%n-%s-%e.png"));
}

TEST(FilePatternTest, ExpandPatternUnsafeChars) {
  EXPECT_EQ(ExpandPattern("scan-%s.%e", 1, "[\"name\"] <> end", "jpg"),
            base::FilePath("scan-__name______end.jpg"));
  EXPECT_EQ(ExpandPattern("scan-%s.%e", 2, "[\"name\"] <> end", "jpg"),
            base::FilePath("scan-__name______end_page2.jpg"));
}

}  // namespace
}  // namespace lorgnette::cli
