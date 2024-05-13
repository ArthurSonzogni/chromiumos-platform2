// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchmaker/file_util.h"

#include <sys/stat.h>

#include <optional>

#include <base/files/file_util.h>
#include <base/hash/md5.h>
#include <brillo/secure_blob.h>

namespace util {

bool IsFile(const base::FilePath& path) {
  struct stat path_stat;
  stat(path.value().data(), &path_stat);
  return S_ISREG(path_stat.st_mode);
}

std::optional<brillo::Blob> ReadFileToBlob(const base::FilePath& path) {
  std::string file_contents;
  brillo::Blob blob;
  if (!base::ReadFileToString(path, &file_contents))
    return std::nullopt;

  return brillo::BlobFromString(file_contents);
}

std::string GetMD5SumForFile(const base::FilePath& path) {
  std::string file_contents;
  if (!base::ReadFileToString(path, &file_contents)) {
    return "";
  }

  return base::MD5String(file_contents);
}

base::FilePath AppendRelativePathOn(const base::FilePath& parent_path,
                                    const base::FilePath& child_path,
                                    const base::FilePath& base_path) {
  base::FilePath new_path = base_path;
  parent_path.AppendRelativePath(child_path, &new_path);

  return new_path;
}

}  // namespace util
