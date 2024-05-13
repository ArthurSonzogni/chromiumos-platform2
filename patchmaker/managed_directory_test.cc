// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gtest/gtest.h>

#include "patchmaker/directory_util.h"
#include "patchmaker/managed_directory.h"

const int kNumDirsInRoot = 3;
const int kNumFilesPerDir = 5;

// We will generate a tree of numbered dirs/files, for instance root/dir2/file3
constexpr char kDirPrefix[] = "dir";
constexpr char kFilePrefix[] = "file";

static const char test_data[] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
    "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate ";

class ManagedDirectoryTest : public ::testing::Test {
 public:
  base::ScopedTempDir root_test_directory_;

  ManagedDirectoryTest() {
    base::FilePath sub_path, test_file;

    // Create a root directory
    CHECK(root_test_directory_.CreateUniqueTempDir());

    base::File::Error error;
    // Generate kNumDirsInRoot directories with kNumFilesPerDir files in each
    for (int i = 0; i < kNumDirsInRoot; i++) {
      sub_path =
          root_test_directory_.GetPath().Append(kDirPrefix + std::to_string(i));
      CHECK(CreateDirectoryAndGetError(sub_path, &error));
      // Create directory in root for all kNumFilesaPerDir to live
      for (int j = 0; j < kNumFilesPerDir; j++) {
        test_file = sub_path.Append(kFilePrefix + std::to_string(j));

        // The test files are almost identical, but with a unique path +
        // filename string at the end. If the test data is long enough, this
        // will ensure that bsdiff will produce patches with the test data. We
        // will verify that we generate at least one patch in
        // TestEncodeToDirectory, below.
        CHECK(base::WriteFile(
            test_file, test_data + sub_path.value() + test_file.value()));
      }
    }

    // Verify that our files were created
    util::SortableFileList file_entries =
        util::GetFilesInDirectory(root_test_directory_.GetPath());
    CHECK_EQ(file_entries.size(), kNumDirsInRoot * kNumFilesPerDir);
  }

  void TestEncodeToDirectory(const base::FilePath& src_path,
                             const base::FilePath& dest_path) {
    ManagedDirectory managed_dir;
    ASSERT_TRUE(managed_dir.CreateNew(dest_path, std::nullopt));
    ASSERT_TRUE(managed_dir.Encode(src_path, dest_path));

    // Verify source and dest have the same number of files (plus one, to
    // account for the patch manifest in dest)
    ASSERT_EQ(util::GetFilesInDirectory(src_path).size() + 1,
              util::GetFilesInDirectory(dest_path).size());

    int num_patches = 0;
    for (const auto& entry : util::GetFilesInDirectory(dest_path)) {
      // Verify that we have patches in the encoded directory
      if (entry.first.value().find(kPatchExtension) != std::string::npos) {
        num_patches++;
      }
    }
    ASSERT_GT(num_patches, 0);
  }

  base::FilePath GetTestPath() { return root_test_directory_.GetPath(); }
};

TEST_F(ManagedDirectoryTest, FullEncodeDecode) {
  base::ScopedTempDir tmp_encode, tmp_decode;
  ASSERT_TRUE(tmp_encode.CreateUniqueTempDir());
  ASSERT_TRUE(tmp_decode.CreateUniqueTempDir());

  base::FilePath src_path = GetTestPath();

  // Encode from src_path to tmp_encode
  TestEncodeToDirectory(src_path, tmp_encode.GetPath());

  // Call DecodeDirectory from tmp_encode to tmp_decode
  ManagedDirectory managed_dir;
  ASSERT_TRUE(managed_dir.CreateFromExisting(tmp_encode.GetPath()));
  ASSERT_TRUE(managed_dir.Decode(tmp_encode.GetPath(), tmp_decode.GetPath()));

  // Ensure src_path and tmp_decode paths have identical contents
  ASSERT_TRUE(util::DirectoriesAreEqual(src_path, tmp_decode.GetPath()));
}

TEST_F(ManagedDirectoryTest, FullEncodeFromManifest) {
  base::ScopedTempDir tmp_encode_fresh, tmp_encode_from_manifest;
  ASSERT_TRUE(tmp_encode_fresh.CreateUniqueTempDir());
  ASSERT_TRUE(tmp_encode_from_manifest.CreateUniqueTempDir());

  base::FilePath src_path = GetTestPath();

  // Encode from src_path , to generate a patch manifest
  TestEncodeToDirectory(src_path, tmp_encode_fresh.GetPath());

  // Call for Encode a second time, using the patch manifest from the first
  ManagedDirectory managed_dir;
  ASSERT_TRUE(managed_dir.CreateNew(
      tmp_encode_from_manifest.GetPath(),
      tmp_encode_fresh.GetPath().Append(kPatchManifestFilename)));
  ASSERT_TRUE(managed_dir.Encode(src_path, tmp_encode_from_manifest.GetPath()));

  // Ensure following the recipe from a precomputed manifest results in an
  // identical output directory as a fresh computation
  ASSERT_TRUE(util::DirectoriesAreEqual(tmp_encode_fresh.GetPath(),
                                        tmp_encode_from_manifest.GetPath()));
}

TEST_F(ManagedDirectoryTest, PartialDecodeSubpath) {
  base::ScopedTempDir tmp_encode, tmp_decode;
  ASSERT_TRUE(tmp_encode.CreateUniqueTempDir());
  ASSERT_TRUE(tmp_decode.CreateUniqueTempDir());

  base::FilePath src_path = GetTestPath();

  // Encode from src_path to tmp_encode
  TestEncodeToDirectory(src_path, tmp_encode.GetPath());

  // Call DecodeDirectory from tmp_encode to tmp_decode with a single directory
  // root/dir1 as target path
  ManagedDirectory managed_dir;
  base::FilePath target_path =
      tmp_encode.GetPath().Append(kDirPrefix + std::to_string(1));
  ASSERT_TRUE(managed_dir.CreateFromExisting(target_path));
  ASSERT_TRUE(managed_dir.Decode(target_path, tmp_decode.GetPath()));

  // Ensure only kNumFilesPerDir files are present in destination directory
  ASSERT_EQ(util::GetFilesInDirectory(tmp_decode.GetPath()).size(),
            kNumFilesPerDir);
}

TEST_F(ManagedDirectoryTest, PartialDecodeOneFile) {
  base::ScopedTempDir tmp_encode, tmp_decode;
  ASSERT_TRUE(tmp_encode.CreateUniqueTempDir());
  ASSERT_TRUE(tmp_decode.CreateUniqueTempDir());

  base::FilePath src_path = GetTestPath();

  // Encode from src_path to tmp_encode
  TestEncodeToDirectory(src_path, tmp_encode.GetPath());

  // Call DecodeDirectory from tmp_encode to tmp_decode with a single file
  // root/dir1/file1 as target path
  ManagedDirectory managed_dir;
  base::FilePath target_path = tmp_encode.GetPath()
                                   .Append(kDirPrefix + std::to_string(1))
                                   .Append(kFilePrefix + std::to_string(1));
  ASSERT_TRUE(managed_dir.CreateFromExisting(target_path));
  ASSERT_TRUE(managed_dir.Decode(target_path, tmp_decode.GetPath()));

  // Ensure only kNumFilesPerDir files are present in destination directory
  ASSERT_EQ(util::GetFilesInDirectory(tmp_decode.GetPath()).size(), 1);
}
