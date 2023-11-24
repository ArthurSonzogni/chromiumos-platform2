// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/base/file_utils.h"

#include <base/files/file_util.h>
#include <base/no_destructor.h>
#include <base/strings/string_util.h>

namespace diagnostics {

#ifndef USE_TEST
static_assert(false,
              "USE_TEST is not defined. Did you set right gn dependency?");
#elif USE_TEST == true
namespace {
base::FilePath& RootDir() {
  static base::NoDestructor<base::FilePath> root_dir{};
  return *root_dir;
}
}  // namespace

base::FilePath GetRootDir() {
  if (RootDir().empty())
    return base::FilePath{"/"};
  return RootDir();
}

base::FilePath GetRootedPath(base::FilePath path) {
  CHECK(!path.empty());
  CHECK(path.IsAbsolute());
  const base::FilePath& root_dir = RootDir();
  // If the path is not overridden, don't modify the path.
  if (root_dir.empty())
    return path;

  CHECK(!root_dir.IsParent(path))
      << "The path is already under the test root " << root_dir;
  // Special case for who only want to get the root dir, which is not supported
  // by `AppendRelativePath()`.
  if (path == base::FilePath("/"))
    return root_dir;
  base::FilePath res = root_dir;
  CHECK(base::FilePath("/").AppendRelativePath(path, &res))
      << "Cannot append path " << path << " to " << root_dir
      << " related to /.";
  return res;
}

ScopedRootDirOverrides::ScopedRootDirOverrides() {
  CHECK(temp_dir_.CreateUniqueTempDir());
  CHECK(RootDir().empty()) << "Cannot set twice.";
  RootDir() = temp_dir_.GetPath();
}

ScopedRootDirOverrides::~ScopedRootDirOverrides() {
  RootDir() = base::FilePath{};
}
#endif

template <>
bool ReadAndTrimString<std::string>(const base::FilePath& file_path,
                                    std::string* out) {
  DCHECK(out);

  if (!base::ReadFileToString(file_path, out))
    return false;

  base::TrimWhitespaceASCII(*out, base::TRIM_ALL, out);
  return true;
}

}  // namespace diagnostics
