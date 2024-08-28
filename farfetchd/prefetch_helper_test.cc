// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mman.h>
#include <sysexits.h>

#include <memory>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/rand_util.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "farfetchd/daemon.h"
#include "libstorage/platform/mock_platform.h"
#include "libstorage/platform/platform.h"

namespace farfetchd {

class PrefetchHelperTest : public testing::Test {
 public:
  PrefetchHelperTest() {}
  ~PrefetchHelperTest() override = default;

 protected:
  std::unique_ptr<libstorage::MockPlatform> platform;
  std::unique_ptr<PrefetchHelper> helper;

  base::ScopedTempDir temp_dir;
  base::FilePath temp_file;

  void SetUp() override {
    platform = std::make_unique<libstorage::MockPlatform>();
    helper = std::make_unique<PrefetchHelper>(platform.get());

    // Temp file setup
    ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
    ASSERT_TRUE(base::PathExists(temp_dir.GetPath()));
    ASSERT_TRUE(base::CreateTemporaryFileInDir(temp_dir.GetPath(), &temp_file));
    ASSERT_TRUE(platform->WriteStringToFile(temp_file, "hello"));
  }
};

TEST_F(PrefetchHelperTest, PreloadFileTest) {
  EXPECT_CALL(*platform, PreadFile(_, _, 5, 0)).Times(1);
  ASSERT_TRUE(helper->PreloadFile(temp_file));
}

TEST_F(PrefetchHelperTest, PreloadFileAsyncTest) {
  EXPECT_CALL(*platform, ReadaheadFile(_, 0, 5)).Times(1);
  ASSERT_TRUE(helper->PreloadFileAsync(temp_file));
}

TEST_F(PrefetchHelperTest, PreloadFileMmapTest) {
  EXPECT_CALL(*platform, MmapFile(_, 5, PROT_READ,
                                  MAP_FILE | MAP_POPULATE | MAP_SHARED, _, 0))
      .Times(1);
  ASSERT_TRUE(helper->PreloadFileMmap(temp_file));
}

}  // namespace farfetchd
