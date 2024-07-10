// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchmaker/directory_util.h"

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

#include "patchmaker/file_util.h"

namespace util {

bool CopyEmptyTreeToDirectory(const base::FilePath& src_path,
                              const base::FilePath& dest_path) {
  base::FilePath dest_path_absl, dir_path;
  base::File::Error error;

  base::FileEnumerator f_e(base::FilePath(src_path), /*recursive=*/true,
                           base::FileEnumerator::DIRECTORIES);

  for (dir_path = f_e.Next(); !dir_path.empty(); dir_path = f_e.Next()) {
    dest_path_absl = AppendRelativePathOn(src_path, dir_path, dest_path);
    if (base::DirectoryExists(dest_path_absl)) {
      continue;
    }

    if (!CreateDirectoryAndGetError(dest_path_absl, &error)) {
      return false;
    }
  }

  return true;
}

bool DirectoriesAreEqual(const base::FilePath& path_one,
                         const base::FilePath& path_two) {
  base::FilePath path_to_check, file_path;
  // Quickly check if sizes mismatch
  if (ComputeDirectorySize(path_one) != ComputeDirectorySize(path_two)) {
    LOG(ERROR) << "Directory sizes don't match";
    return false;
  }
  // Validate each file's contents
  SortableFileList file_entries;
  base::FileEnumerator f_e(path_one, /*recursive=*/true,
                           base::FileEnumerator::FILES);
  for (file_path = f_e.Next(); !file_path.empty(); file_path = f_e.Next()) {
    // Apply the relative path between src_path and file_path onto dest_path
    path_to_check = AppendRelativePathOn(path_one, file_path, path_two);

    if (!base::PathExists(file_path) || !base::PathExists(path_to_check)) {
      LOG(ERROR) << "File " << file_path << " is missing at the destination...";
      return false;
    }
    if (!ContentsEqual(file_path, path_to_check)) {
      LOG(ERROR) << "Files don't match: " << file_path << " and "
                 << path_to_check;
      return false;
    }
  }
  return true;
}

SortableFileList GetFilesInDirectory(const base::FilePath& src_path) {
  SortableFileList file_entries;
  base::FileEnumerator f_e(src_path, /*recursive=*/true,
                           base::FileEnumerator::FILES);

  base::FilePath file_path;
  int64_t file_size;
  for (file_path = f_e.Next(); !file_path.empty(); file_path = f_e.Next()) {
    base::GetFileSize(file_path, &file_size);
    file_entries.emplace_back(file_path, file_size);
  }

  return file_entries;
}

}  // namespace util
