// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_BLKDEV_UTILS_UFS_H_
#define LIBBRILLO_BRILLO_BLKDEV_UTILS_UFS_H_

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace brillo {

BRILLO_EXPORT base::FilePath UfsSysfsToControllerNode(
    const base::FilePath& dev_node);

BRILLO_EXPORT bool IsUfs(const base::FilePath& dev_node);

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_BLKDEV_UTILS_UFS_H_
