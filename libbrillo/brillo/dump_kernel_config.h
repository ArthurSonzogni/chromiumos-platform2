// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_DUMP_KERNEL_CONFIG_H_
#define LIBBRILLO_BRILLO_DUMP_KERNEL_CONFIG_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace brillo {

// Conveniently invoke the external dump_kernel_config library.
BRILLO_EXPORT std::optional<std::string> DumpKernelConfig(
    const base::FilePath& kernel_dev);

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_DUMP_KERNEL_CONFIG_H_
