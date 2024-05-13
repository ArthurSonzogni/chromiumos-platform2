// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHMAKER_DIRECTORY_UTIL_H_
#define PATCHMAKER_DIRECTORY_UTIL_H_

#include <utility>
#include <vector>

#include <base/files/file_path.h>

namespace util {

using SortableFileList = std::vector<std::pair<base::FilePath, int>>;

bool CopyEmptyTreeToDirectory(const base::FilePath& src_path,
                              const base::FilePath& dest_path);

bool DirectoriesAreEqual(const base::FilePath& path_one,
                         const base::FilePath& path_two);

SortableFileList GetFilesInDirectory(const base::FilePath& src_path);

}  // namespace util

#endif  // PATCHMAKER_DIRECTORY_UTIL_H_
