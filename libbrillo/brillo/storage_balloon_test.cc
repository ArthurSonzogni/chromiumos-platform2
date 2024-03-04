// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/storage_balloon.h"

#include <sys/statvfs.h>
#include <sys/vfs.h>

#include <string>
#include <unordered_map>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace brillo {

class FakeStorageBalloon : public StorageBalloon {
 public:
  FakeStorageBalloon(uint64_t remaining_size, const base::FilePath& path)
      : StorageBalloon(path, path.AppendASCII("balloon")),
        remaining_size_(remaining_size) {}

 protected:
  bool SetBalloonSize(int64_t size) override {
    if (!StorageBalloon::SetBalloonSize(size))
      return false;

    remaining_size_ = remaining_size_ - (size - current_balloon_size_);
    current_balloon_size_ = size;
    return true;
  }

  bool StatVfs(struct statvfs* buf) override {
    buf->f_bsize = 4096;
    buf->f_blocks = (remaining_size_ + current_balloon_size_) / 4096;
    buf->f_bavail = remaining_size_ / 4096;
    return true;
  }

 private:
  int64_t current_balloon_size_ = 0;
  int64_t remaining_size_;
};

TEST(StorageBalloon, InvalidPath) {
  FakeStorageBalloon f(4096, base::FilePath("/a/b/c"));
  EXPECT_EQ(f.IsValid(), false);
}

TEST(StorageBalloon, ValidPath) {
  base::ScopedTempDir dir;

  int64_t fs_size = 4ULL * 1024 * 1024 * 1024;

  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::WriteFile(dir.GetPath().AppendASCII("balloon"), "4096");
  FakeStorageBalloon f(fs_size, dir.GetPath());
  EXPECT_EQ(f.IsValid(), true);
}

TEST(StorageBalloonTest, FullInflation) {
  base::ScopedTempDir dir;

  int64_t fs_size = 4ULL * 1024 * 1024 * 1024;
  int64_t target_space = 1ULL * 1024 * 1024 * 1024;

  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::WriteFile(dir.GetPath().AppendASCII("balloon"), "4096");
  FakeStorageBalloon f(fs_size, dir.GetPath());
  EXPECT_EQ(f.IsValid(), true);

  EXPECT_TRUE(f.Adjust(target_space));
  EXPECT_EQ(f.GetCurrentBalloonSize(), fs_size - target_space);
  EXPECT_TRUE(f.Adjust(fs_size));
  EXPECT_EQ(f.GetCurrentBalloonSize(), 0);
}

TEST(StorageBalloonTest, FullDeflation) {
  base::ScopedTempDir dir;

  int64_t fs_size = 4ULL * 1024 * 1024 * 1024;
  int64_t target_space = 512ULL * 1024 * 1024;

  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::WriteFile(dir.GetPath().AppendASCII("balloon"), "4096");
  FakeStorageBalloon f(fs_size, dir.GetPath());
  EXPECT_EQ(f.IsValid(), true);

  EXPECT_TRUE(f.Adjust(target_space));
  EXPECT_EQ(f.GetCurrentBalloonSize(), fs_size - target_space);

  EXPECT_TRUE(f.Deflate());
  EXPECT_EQ(f.GetCurrentBalloonSize(), 0);
}

TEST(StorageBalloonTest, Adjustment) {
  base::ScopedTempDir dir;

  int64_t fs_size = 4ULL * 1024 * 1024 * 1024;
  int64_t target_space = 1ULL * 1024 * 1024 * 1024;
  int64_t updated_target_space = 400ULL * 1024 * 1024;

  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::WriteFile(dir.GetPath().AppendASCII("balloon"), "4096");
  FakeStorageBalloon f(fs_size, dir.GetPath());
  EXPECT_EQ(f.IsValid(), true);

  EXPECT_TRUE(f.Adjust(target_space));
  EXPECT_EQ(f.GetCurrentBalloonSize(), fs_size - target_space);

  EXPECT_TRUE(f.Adjust(updated_target_space));
  EXPECT_EQ(f.GetCurrentBalloonSize(), fs_size - updated_target_space);
}

}  // namespace brillo
