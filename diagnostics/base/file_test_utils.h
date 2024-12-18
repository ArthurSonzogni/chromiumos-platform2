// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BASE_FILE_TEST_UTILS_H_
#define DIAGNOSTICS_BASE_FILE_TEST_UTILS_H_

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/base/path_literal.h"

namespace diagnostics {

// Write |file_contents| into file located in |file_path|. Also will create all
// nested parent directories if necessary.
bool WriteFileAndCreateParentDirs(const base::FilePath& file_path,
                                  const std::string& file_contents);

// Write |file_contents| into file located in |file_path|, then create a
// symbolic link which points to |file_path|. Will create all nested parent
// directories if necessary.
bool WriteFileAndCreateSymbolicLink(const base::FilePath& file_path,
                                    const std::string& file_contents,
                                    const base::FilePath& symlink_path);

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
    PathType(const base::FilePath& path)  // NOLINT(runtime/explicit)
        : file_path_(path) {}
    PathType(base::FilePath&& path)  // NOLINT(runtime/explicit)
        : file_path_(std::move(path)) {}
    PathType(PathLiteral path)  // NOLINT(runtime/explicit)
        : file_path_(path.ToPath()) {}
    template <std::size_t Size>
    PathType(StaticPathLiteral<Size> path)  // NOLINT(runtime/explicit)
        : file_path_(path.ToPath()) {}
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

 public:
  BaseFileTest(const BaseFileTest&) = delete;
  BaseFileTest& operator=(const BaseFileTest&) = delete;

 protected:
  BaseFileTest() = default;

  // Unsets a file or a directory in the test rootfs.
  void UnsetPath(const PathType& path) const;
  // Creates a symbolic link at |path| which points to |target|. The parent
  // directories will be created if they don't exist.
  void SetSymbolicLink(const PathType& target, const PathType& path);
  // Turns the path into the path under the test rootfs. This should work for
  // both absolute and relative path.
  base::FilePath GetPathUnderRoot(const PathType& path) const;

  // Creates a file in the test rootfs. The parent directories will be created
  // if they don't exist. |ContentType| can be |std::string| or
  // |base::span<const uint8_t>| (for binary data).
  template <typename ContentType>
  void SetFile(const PathType& path, ContentType&& content) const {
    ASSERT_FALSE(GetRootDir().empty());
    auto file = GetPathUnderRoot(path);
    ASSERT_TRUE(base::CreateDirectory(file.DirName()));
    ASSERT_TRUE(base::WriteFile(file, std::forward<ContentType>(content)));
  }

  // Sets fake cros config data. If `nullopt` is passed the cros config field
  // will be removed.
  void SetFakeCrosConfig(const PathType& path,
                         const std::optional<std::string>& data);

 private:
  std::unique_ptr<ScopedRootDirOverrides> scoped_root_dir_ =
      std::make_unique<ScopedRootDirOverrides>();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_BASE_FILE_TEST_UTILS_H_
