// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/base/path_literal.h"

#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"

namespace diagnostics {
namespace {

using PathUtilsTest = BaseFileTest;

TEST_F(PathUtilsTest, BaseTest) {
  // Note: This test use constexpr to test compile time detutions.
  constexpr auto kExamplePath = MakePathLiteral("a", "b", "c");
  EXPECT_EQ(kExamplePath.ToPath(), base::FilePath("a/b/c"));

  constexpr auto kAppendExamplePath = MakePathLiteral(kExamplePath, "a");
  EXPECT_EQ(kAppendExamplePath.ToPath(), base::FilePath("a/b/c/a"));

  constexpr auto kConcatExamplePath =
      MakePathLiteral(kExamplePath, kExamplePath);
  EXPECT_EQ(kConcatExamplePath.ToPath(), base::FilePath("a/b/c/a/b/c"));
}

}  // namespace
}  // namespace diagnostics
