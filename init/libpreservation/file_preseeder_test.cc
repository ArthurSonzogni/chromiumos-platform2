// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/libpreservation/file_preseeder.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/files/file_util.h"
#include "init/libpreservation/fake_ext2fs.h"
#include "init/libpreservation/filesystem_manager.h"

using ::testing::_;
using ::testing::Return;

namespace libpreservation {
namespace {
const base::FilePath kFoo = base::FilePath("foo");
const base::FilePath kBarBaz = base::FilePath("bar/baz");
const base::FilePath kBarFoo = base::FilePath("bar/foo");
const base::FilePath kBarFooAr = base::FilePath("bar/foo/ar");
const base::FilePath kBaz = base::FilePath("baz");
const base::FilePath kBar = base::FilePath("bar");
}  // namespace

class FilePreseederTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    metadata_path_ = temp_dir_.GetPath().Append("metadata");
    fs_root_ = base::FilePath("/");
    mount_root_ = temp_dir_.GetPath().Append("mount_root");
    ASSERT_TRUE(base::CreateDirectory(fs_root_));
    ASSERT_TRUE(base::CreateDirectory(mount_root_));
    fs_ = FakeExt2fs::Create(base::FilePath("/dev/null"));
    fs_manager_ = std::make_unique<FilesystemManager>(std::move(fs_));
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath metadata_path_;
  base::FilePath fs_root_;
  base::FilePath mount_root_;
  std::unique_ptr<Ext2fs> fs_;
  std::unique_ptr<FilesystemManager> fs_manager_;
};

TEST_F(FilePreseederTest, SaveFileState) {
  std::set<base::FilePath> file_allowlist = {kFoo, kBarBaz};
  std::set<base::FilePath> directory_allowlist = {kBar};
  FilePreseeder preseeder(directory_allowlist, fs_root_, mount_root_,
                          metadata_path_);
  base::FilePath file_foo = mount_root_.Append(kFoo);
  base::FilePath file_baz = mount_root_.Append(kBarBaz);
  ASSERT_TRUE(base::CreateDirectory(mount_root_.Append(kBar)));
  ASSERT_TRUE(brillo::WriteStringToFile(file_foo, "foo"));
  ASSERT_TRUE(brillo::WriteStringToFile(file_baz, "baz"));
  EXPECT_TRUE(preseeder.SaveFileState(file_allowlist));
  EXPECT_TRUE(base::PathExists(metadata_path_));
}

TEST_F(FilePreseederTest, SaveFileStateExtent) {
  std::set<base::FilePath> file_allowlist = {kFoo, kBarBaz};
  std::set<base::FilePath> directory_allowlist = {kBar};
  FilePreseeder preseeder(directory_allowlist, fs_root_, mount_root_,
                          metadata_path_);
  base::FilePath file_foo = mount_root_.Append(kFoo);
  base::FilePath file_baz = mount_root_.Append(kBarBaz);
  ASSERT_TRUE(base::CreateDirectory(mount_root_.Append(kBar)));
  std::string data(4096, 'a');
  ASSERT_TRUE(brillo::WriteStringToFile(file_foo, data));
  ASSERT_TRUE(brillo::WriteStringToFile(file_baz, data));
  EXPECT_TRUE(preseeder.SaveFileState(file_allowlist));
  EXPECT_TRUE(base::PathExists(metadata_path_));
}

TEST_F(FilePreseederTest, LoadMetadata) {
  std::set<base::FilePath> file_allowlist = {kFoo, kBarBaz};
  std::set<base::FilePath> directory_allowlist = {kBar};
  FilePreseeder preseeder(directory_allowlist, fs_root_, mount_root_,
                          metadata_path_);
  base::FilePath file_foo = mount_root_.Append(kFoo);
  base::FilePath file_baz = mount_root_.Append(kBarBaz);
  ASSERT_TRUE(base::CreateDirectory(mount_root_.Append(kBar)));
  ASSERT_TRUE(brillo::WriteStringToFile(file_foo, "foo"));
  ASSERT_TRUE(brillo::WriteStringToFile(file_baz, "baz"));
  EXPECT_TRUE(preseeder.SaveFileState(file_allowlist));
  EXPECT_TRUE(base::PathExists(metadata_path_));

  FilePreseeder preseeder2(directory_allowlist, fs_root_, mount_root_,
                           metadata_path_);
  EXPECT_TRUE(preseeder2.LoadMetadata());
}

TEST_F(FilePreseederTest, CreateDirectoryRecursively) {
  std::set<base::FilePath> file_allowlist = {kFoo, kBarBaz};
  std::set<base::FilePath> directory_allowlist = {kBar};
  FilePreseeder preseeder(directory_allowlist, fs_root_, mount_root_,
                          metadata_path_);
  EXPECT_TRUE(preseeder.CreateDirectoryRecursively(fs_manager_.get(), kBarBaz));
  EXPECT_TRUE(fs_manager_->FileExists(fs_root_.Append(kBarBaz)));
  EXPECT_TRUE(fs_manager_->FileExists(fs_root_.Append(kBar)));
}

TEST_F(FilePreseederTest, RestoreExtentFiles) {
  std::set<base::FilePath> file_allowlist = {kFoo, kBarBaz};
  std::set<base::FilePath> directory_allowlist = {kBar};
  FilePreseeder preseeder(directory_allowlist, fs_root_, mount_root_,
                          metadata_path_);
  base::FilePath file_foo = mount_root_.Append(kFoo);
  base::FilePath file_baz = mount_root_.Append(kBarBaz);
  ASSERT_TRUE(base::CreateDirectory(mount_root_.Append(kBar)));
  std::string data(4096, 'a');
  ASSERT_TRUE(brillo::WriteStringToFile(file_foo, data));
  ASSERT_TRUE(brillo::WriteStringToFile(file_baz, data));
  EXPECT_TRUE(preseeder.SaveFileState(file_allowlist));
  EXPECT_TRUE(base::PathExists(metadata_path_));

  FilePreseeder preseeder2(directory_allowlist, fs_root_, mount_root_,
                           metadata_path_);
  EXPECT_TRUE(preseeder2.LoadMetadata());
  EXPECT_TRUE(preseeder2.RestoreExtentFiles(fs_manager_.get()));
  EXPECT_TRUE(fs_manager_->FileExists(fs_root_.Append(kBarBaz)));
  EXPECT_FALSE(fs_manager_->FileExists(fs_root_.Append(kFoo)));
}

TEST_F(FilePreseederTest, RestoreInlineFiles) {
  std::set<base::FilePath> file_allowlist = {kFoo, kBarBaz};
  std::set<base::FilePath> directory_allowlist = {kBar};
  FilePreseeder preseeder(directory_allowlist, fs_root_, mount_root_,
                          metadata_path_);
  base::FilePath file_foo = mount_root_.Append(kFoo);
  base::FilePath file_baz = mount_root_.Append(kBarBaz);
  ASSERT_TRUE(base::CreateDirectory(mount_root_.Append(kBar)));
  ASSERT_TRUE(brillo::WriteStringToFile(file_foo, "foo"));
  ASSERT_TRUE(brillo::WriteStringToFile(file_baz, "baz"));
  EXPECT_TRUE(preseeder.SaveFileState(file_allowlist));
  EXPECT_TRUE(base::PathExists(metadata_path_));

  FilePreseeder preseeder2(directory_allowlist, fs_root_, mount_root_,
                           metadata_path_);
  EXPECT_TRUE(preseeder2.LoadMetadata());
  EXPECT_TRUE(preseeder2.RestoreInlineFiles());
  EXPECT_TRUE(base::PathExists(file_foo));
  EXPECT_TRUE(base::PathExists(file_baz));
}

TEST_F(FilePreseederTest, CheckAllowlist) {
  std::set<base::FilePath> directory_allowlist = {kBarFoo};
  FilePreseeder preseeder(directory_allowlist, fs_root_, mount_root_,
                          metadata_path_);
  EXPECT_FALSE(preseeder.CheckAllowlist(kFoo));
  EXPECT_FALSE(preseeder.CheckAllowlist(kBarBaz));
  EXPECT_TRUE(preseeder.CheckAllowlist(kBarFoo));
  EXPECT_TRUE(preseeder.CheckAllowlist(kBarFooAr));

  EXPECT_FALSE(preseeder.CheckAllowlist(kBaz));
}

}  // namespace libpreservation
