// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <gtest/gtest.h>

namespace {
std::string ReadFile(const base::FilePath& path) {
  std::string content;
  EXPECT_TRUE(base::ReadFileToString(path, &content));
  return content;
}

// Count the number of entries in a directory. Include both files and
// directories, but not '.' and '..'.
int CountDirEntries(const base::FilePath& path) {
  base::FileEnumerator e(
      path, /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  int count = 0;
  e.ForEach([&](const base::FilePath&) { count++; });
  return count;
}

}  // namespace

class EncryptedImportTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    const auto root = scoped_temp_dir_.GetPath();
    from_dir_ = root.Append("from");
    to_dir_ = root.Append("to");
    validation_path_ = root.Append("validation");

    ASSERT_TRUE(base::CreateDirectory(from_dir_));
    ASSERT_TRUE(base::CreateDirectory(to_dir_));

    // Create some test data.
    ASSERT_TRUE(base::WriteFile(from_dir_.Append("file1"), "file1 data"));
    ASSERT_TRUE(base::WriteFile(from_dir_.Append("file2"), "file2 data"));
    ASSERT_TRUE(base::WriteFile(from_dir_.Append("file3"), "file3 data"));
    // Create a validation file that just contains file1 and file2.
    ASSERT_TRUE(base::WriteFile(validation_path_,
                                "41d2f1c5ed3a4096025f53cd400eaacc8f9cf9c771f23c"
                                "1dcb3b2770218cd3e3 file1\n"
                                "eeaf82b6a63eee1e6cbb680a4bf5056ffb0b64bc0d761a"
                                "c0608fb63378f80de1 file2\n"));
  }

  // Run the encrypted_import script. Returns true if the script is
  // successful, false otherwise.
  bool RunEncryptedImport() {
    brillo::ProcessImpl proc;
    proc.AddArg("encrypted_import");
    proc.AddArg(from_dir_.value());
    proc.AddArg(validation_path_.value());
    proc.AddArg(to_dir_.value());
    const auto code = proc.Run();
    return code == 0;
  }

 protected:
  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath from_dir_;
  base::FilePath to_dir_;
  base::FilePath validation_path_;
};

TEST_F(EncryptedImportTest, Success) {
  ASSERT_TRUE(RunEncryptedImport());

  // Check that the files were copied over correctly.
  EXPECT_EQ(ReadFile(to_dir_.Append("file1")), "file1 data");
  EXPECT_EQ(ReadFile(to_dir_.Append("file2")), "file2 data");

  // Check that there are no other files in the output directory.
  EXPECT_EQ(CountDirEntries(to_dir_), 2);
}

TEST_F(EncryptedImportTest, BadChecksum) {
  ASSERT_TRUE(base::WriteFile(from_dir_.Append("file2"), "file2 modified"));
  ASSERT_FALSE(RunEncryptedImport());
  // Check that the output directory is empty.
  EXPECT_EQ(CountDirEntries(to_dir_), 0);
}

TEST_F(EncryptedImportTest, MissingFile) {
  ASSERT_TRUE(brillo::DeleteFile(from_dir_.Append("file2")));
  ASSERT_FALSE(RunEncryptedImport());
  // Check that the output directory is empty.
  EXPECT_EQ(CountDirEntries(to_dir_), 0);
}
