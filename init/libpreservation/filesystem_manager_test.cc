// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gtest/gtest.h>
#include <init/libpreservation/fake_ext2fs.h>
#include <init/libpreservation/filesystem_manager.h>
#include <init/libpreservation/preseeded_files.pb.h>

namespace libpreservation {

class FilesystemManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs_ = FakeExt2fs::Create(base::FilePath("/dev/null"));
    fs_manager_ = std::make_unique<FilesystemManager>(std::move(fs_));
  }

 protected:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<Ext2fs> fs_;
  std::unique_ptr<FilesystemManager> fs_manager_;
};

TEST_F(FilesystemManagerTest, CreateDirectory) {
  EXPECT_TRUE(fs_manager_->CreateDirectory(base::FilePath("/foo")));
  EXPECT_FALSE(fs_manager_->CreateDirectory(base::FilePath("/foo/bar/baz")));
  EXPECT_TRUE(fs_manager_->CreateDirectory(base::FilePath("/foo/bar")));
  EXPECT_TRUE(fs_manager_->CreateDirectory(base::FilePath("/foo/bar/baz")));
  EXPECT_FALSE(fs_manager_->CreateDirectory(base::FilePath(".")));
  EXPECT_FALSE(fs_manager_->CreateDirectory(base::FilePath("..")));
}

TEST_F(FilesystemManagerTest, CreateFileAndFixedGoalFallocate) {
  ExtentArray extents;
  Extent* extent = extents.add_extent();
  extent->set_start(0);
  extent->set_length(4096);
  extent->set_goal(1024);
  EXPECT_TRUE(fs_manager_->CreateFileAndFixedGoalFallocate(
      base::FilePath("/foo"), 4096, extents));
  EXPECT_TRUE(fs_manager_->FileExists(base::FilePath("/foo")));
  EXPECT_FALSE(fs_manager_->CreateFileAndFixedGoalFallocate(
      base::FilePath("/foo"), 4096, extents));
  EXPECT_TRUE(fs_manager_->CreateDirectory(base::FilePath("/bar")));

  // /bar will not be created since it has overlapping extents.
  EXPECT_FALSE(fs_manager_->CreateFileAndFixedGoalFallocate(
      base::FilePath("/bar"), 4096, extents));
  EXPECT_FALSE(fs_manager_->FileExists(base::FilePath("/bar/baz")));

  // Invalid file names.
  EXPECT_FALSE(fs_manager_->CreateFileAndFixedGoalFallocate(base::FilePath("."),
                                                            4096, extents));
  EXPECT_FALSE(fs_manager_->CreateFileAndFixedGoalFallocate(
      base::FilePath(".."), 4096, extents));
}

TEST_F(FilesystemManagerTest, UnlinkFile) {
  ExtentArray extents;
  Extent* extent = extents.add_extent();
  extent->set_start(0);
  extent->set_length(4096);
  extent->set_goal(1024);
  EXPECT_TRUE(fs_manager_->CreateFileAndFixedGoalFallocate(
      base::FilePath("/foo"), 4096, extents));
  EXPECT_TRUE(fs_manager_->FileExists(base::FilePath("/foo")));
  EXPECT_TRUE(fs_manager_->UnlinkFile(base::FilePath("/foo")));
  EXPECT_FALSE(fs_manager_->FileExists(base::FilePath("/foo")));
  EXPECT_TRUE(fs_manager_->CreateDirectory(base::FilePath("/bar")));

  extent->set_start(0);
  extent->set_length(4096);
  extent->set_goal(8192);
  EXPECT_TRUE(fs_manager_->CreateFileAndFixedGoalFallocate(
      base::FilePath("/bar/baz"), 4096, extents));
  EXPECT_TRUE(fs_manager_->FileExists(base::FilePath("/bar/baz")));
  EXPECT_TRUE(fs_manager_->UnlinkFile(base::FilePath("/bar/baz")));
  EXPECT_FALSE(fs_manager_->FileExists(base::FilePath("/bar/baz")));
}

TEST_F(FilesystemManagerTest, FileExists) {
  ExtentArray extents;
  Extent* extent = extents.add_extent();
  extent->set_start(0);
  extent->set_length(4096);
  extent->set_goal(1024);
  EXPECT_FALSE(fs_manager_->FileExists(base::FilePath("/foo")));
  EXPECT_TRUE(fs_manager_->CreateFileAndFixedGoalFallocate(
      base::FilePath("/foo"), 4096, extents));
  EXPECT_TRUE(fs_manager_->FileExists(base::FilePath("/foo")));
  EXPECT_TRUE(fs_manager_->CreateDirectory(base::FilePath("/bar")));

  // Overlapping extents.
  EXPECT_FALSE(fs_manager_->FileExists(base::FilePath("/bar/baz")));
  EXPECT_FALSE(fs_manager_->CreateFileAndFixedGoalFallocate(
      base::FilePath("/bar/baz"), 4096, extents));
  EXPECT_FALSE(fs_manager_->FileExists(base::FilePath("/bar/baz")));
}

}  // namespace libpreservation
