// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchmaker/patch_util.h"

#include <cstdint>
#include <optional>

#include <base/files/file_util.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

static const char test_data_old[] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
    "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate ";

// Almost identical to the above, but with one more line. While it isn't binary
// data, it should still allow a patch to be generated that is smaller than the
// original file.
static const char test_data_new[] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
    "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
    "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint ";

TEST(Patch, CompressionEfficacy) {
  base::FilePath old_path, new_path, patch_path;

  base::CreateTemporaryFile(&old_path);
  base::CreateTemporaryFile(&new_path);
  base::CreateTemporaryFile(&patch_path);

  base::WriteFile(old_path, test_data_old);
  base::WriteFile(new_path, test_data_new);

  ASSERT_TRUE(util::DoBsDiff(old_path, new_path, patch_path));

  std::optional<int64_t> file_size_new = base::GetFileSize(new_path);
  std::optional<int64_t> file_size_patch = base::GetFileSize(patch_path);
  ASSERT_TRUE(file_size_new.has_value());
  ASSERT_TRUE(file_size_patch.has_value());

  // Confirm that compressed data is smaller than the original data
  ASSERT_TRUE(file_size_patch.value() < file_size_new.value());
}

TEST(Patch, ReversiblePatch) {
  base::FilePath old_path, new_path, patch_path, reconstructed_path;

  base::CreateTemporaryFile(&old_path);
  base::CreateTemporaryFile(&new_path);
  base::CreateTemporaryFile(&patch_path);
  base::CreateTemporaryFile(&reconstructed_path);

  base::WriteFile(old_path, test_data_old);
  base::WriteFile(new_path, test_data_new);

  ASSERT_TRUE(util::DoBsDiff(old_path, new_path, patch_path));
  ASSERT_TRUE(util::DoBsPatch(old_path, reconstructed_path, patch_path));

  ASSERT_TRUE(base::ContentsEqual(new_path, reconstructed_path));
}
