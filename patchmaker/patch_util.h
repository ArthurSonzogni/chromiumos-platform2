// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHMAKER_PATCH_UTIL_H_
#define PATCHMAKER_PATCH_UTIL_H_

#include <base/files/file_path.h>

namespace util {

bool DoBsDiff(const base::FilePath& old_file,
              const base::FilePath& new_file,
              const base::FilePath& patch_file);

bool DoBsPatch(const base::FilePath& old_file,
               const base::FilePath& new_file,
               const base::FilePath& patch_file);

}  // namespace util

#endif  // PATCHMAKER_PATCH_UTIL_H_
