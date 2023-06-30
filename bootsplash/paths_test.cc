// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/paths.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

using base::FilePath;

namespace {

class PathsTest : public ::testing::Test {
 protected:
  const FilePath& test_dir() const { return scoped_temp_dir_.GetPath(); }

 private:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    paths::SetPrefixForTesting(test_dir());
  }

  base::ScopedTempDir scoped_temp_dir_;
};

TEST_F(PathsTest, Get) {
  paths::SetPrefixForTesting(base::FilePath(""));
  EXPECT_EQ("/run/foo", paths::Get("/run/foo").value());
}

TEST_F(PathsTest, SetPrefixForTesting) {
  paths::SetPrefixForTesting(base::FilePath("/tmp"));
  EXPECT_EQ("/tmp/run/foo", paths::Get("/run/foo").value());
  paths::SetPrefixForTesting(base::FilePath());
  EXPECT_EQ("/run/foo", paths::Get("/run/foo").value());
}

}  // namespace
