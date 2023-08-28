// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_util.h>
#include <gtest/gtest.h>

#include "fbpreprocessor/firmware_dump.h"

namespace fbpreprocessor {

namespace {

class FirmwareDumpTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir tmp_dir_;
};

TEST_F(FirmwareDumpTest, BaseNameSimple) {
  std::string name("test");
  FirmwareDump fw(tmp_dir_.GetPath().Append(name));
  EXPECT_EQ(fw.BaseName(), base::FilePath(name));
}

TEST_F(FirmwareDumpTest, BaseNameDots) {
  std::string name("devcoredump_iwlwifi.20230901.231459.05766.1");
  FirmwareDump fw(tmp_dir_.GetPath().Append(name));
  EXPECT_EQ(fw.BaseName(), base::FilePath(name));
}

TEST_F(FirmwareDumpTest, DumpFileSimple) {
  std::string name("test");
  base::FilePath base_path(tmp_dir_.GetPath().Append(name));
  FirmwareDump fw(base_path);
  EXPECT_EQ(fw.DumpFile(), base_path.AddExtension("dmp"));
}

TEST_F(FirmwareDumpTest, DumpFileDots) {
  std::string name("devcoredump_iwlwifi.20230901.231459.05766.1");
  base::FilePath base_path(tmp_dir_.GetPath().Append(name));
  FirmwareDump fw(base_path);
  EXPECT_EQ(fw.DumpFile(), base_path.AddExtension("dmp"));
}

TEST_F(FirmwareDumpTest, DeleteRemovesFiles) {
  base::FilePath base_path(tmp_dir_.GetPath().Append("test"));

  // Create the .dmp file
  base::FilePath dmp = base_path.AddExtension("dmp");
  ASSERT_TRUE(base::WriteFile(dmp, "testdata"));
  ASSERT_TRUE(base::PathExists(dmp));

  FirmwareDump fw(base_path);
  ASSERT_TRUE(fw.Delete());
  // .dmp file no longer exists.
  ASSERT_FALSE(base::PathExists(dmp));
}

}  // namespace

}  // namespace fbpreprocessor
