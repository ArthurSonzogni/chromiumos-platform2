// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHMAKER_FILE_UTIL_H_
#define PATCHMAKER_FILE_UTIL_H_

#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

namespace util {

bool IsFile(const base::FilePath& path);

std::optional<brillo::Blob> ReadFileToBlob(const base::FilePath& path);

std::string GetMD5SumForFile(const base::FilePath& path);

base::FilePath AppendRelativePathOn(const base::FilePath& parent_path,
                                    const base::FilePath& child_path,
                                    const base::FilePath& base_path);

}  // namespace util

#endif  // PATCHMAKER_FILE_UTIL_H_
