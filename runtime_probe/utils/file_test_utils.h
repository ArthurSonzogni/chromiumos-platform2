// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_FILE_TEST_UTILS_H_
#define RUNTIME_PROBE_UTILS_FILE_TEST_UTILS_H_

#include <string>
#include <string_view>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace runtime_probe {

// A helper class for creating file related unittest.
class BaseFileTest : public ::testing::Test {
 protected:
  // Unittest usually sets a lot of files with literal string constant filename.
  // This helper class can help to convert those constants into
  // |base::FilePath|. So the literal string constants can be used without
  // converting.
  class PathType {
   public:
    PathType() = delete;
    PathType(const PathType& oth) = default;
    PathType(PathType&& oth) = default;

    PathType(const char* path)  // NOLINT(runtime/explicit)
        : file_path_(path) {}
    PathType(const std::string& path)  // NOLINT(runtime/explicit)
        : file_path_(path) {}
    PathType(std::string_view path)  // NOLINT(runtime/explicit)
        : file_path_(path) {}
    PathType(const base::FilePath& path)  // NOLINT(runtime/explicit)
        : file_path_(path) {}
    PathType(base::FilePath&& path)  // NOLINT(runtime/explicit)
        : file_path_(std::move(path)) {}
    // Join each part of path into a single path. For example, {"a/b", "c"} =>
    // "a/b/c". This is convenient for the following case:
    //    SetFile({kDir, kDir2, kFilename}, ...);
    //
    PathType(
        std::initializer_list<std::string> paths);  // NOLINT(runtime/explicit)

    const base::FilePath& file_path() const { return file_path_; }

   private:
    base::FilePath file_path_;
  };

 protected:
  BaseFileTest() = default;
  BaseFileTest(const BaseFileTest&) = delete;
  BaseFileTest& operator=(const BaseFileTest&) = delete;

  // Creates a test root. The test root will be deleted after the test.
  void CreateTestRoot();
  // Sets the test root. It is the caller's responsibility to clean the test
  // root after the test. This is for manually control the test root.
  void SetTestRoot(const base::FilePath& path);
  // Unsets a file or a directory in the test rootfs.
  void UnsetPath(const PathType& path) const;
  // Creates a symbolic link at |path| which points to |target|. The parent
  // directories will be created if they don't exist.
  void SetSymbolicLink(const PathType& target, const PathType& path);
  // Creates a directory for test. The parent directories will be created if
  // they don't exist.
  void SetDirectory(const PathType& path);
  // Turns the path into the path under the test rootfs. This should work for
  // both absolute and relative path.
  base::FilePath GetPathUnderRoot(const PathType& path) const;
  // Returns the path to the static test data directory.
  base::FilePath GetTestDataPath() const;
  // Returns the path of the rootfs for testing.
  const base::FilePath& root_dir() const;

  // Creates a file in the test rootfs. The parent directories will be created
  // if they don't exist. |ContentType| can be |std::string| or
  // |base::span<const uint8_t>| (for binary data).
  template <typename ContentType>
  void SetFile(const PathType& path, ContentType&& content) const {
    ASSERT_FALSE(root_dir_.empty());
    auto file = GetPathUnderRoot(path);
    ASSERT_TRUE(base::CreateDirectory(file.DirName()));
    ASSERT_TRUE(base::WriteFile(file, std::forward<ContentType>(content)));
  }

 private:
  base::FilePath root_dir_;
  base::ScopedTempDir scoped_temp_dir_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_FILE_TEST_UTILS_H_
