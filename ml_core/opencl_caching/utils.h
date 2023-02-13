// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_CORE_OPENCL_CACHING_UTILS_H_
#define ML_CORE_OPENCL_CACHING_UTILS_H_

#include <base/files/file_path.h>

namespace cros {

void InstallPrebuiltCache(const base::FilePath& dlc_root_path);
void ClearCacheDirectory();
void CopyCacheFiles(const base::FilePath& source_dir, bool overwrite_files);

}  // namespace cros

#endif  // ML_CORE_OPENCL_CACHING_UTILS_H_
