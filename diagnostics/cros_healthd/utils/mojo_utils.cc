// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/mojo_utils.h"

#include <utility>

#include <base/files/platform_file.h>
#include <base/logging.h>
#include <mojo/public/cpp/platform/platform_handle.h>
#include <mojo/public/cpp/system/handle.h>
#include <mojo/public/cpp/system/platform_handle.h>

namespace diagnostics::mojo_utils {

base::ScopedPlatformFile UnwrapMojoHandle(mojo::ScopedHandle handle) {
  auto fd = mojo::UnwrapPlatformHandle(std::move(handle)).TakeFD();
  if (!fd.is_valid()) {
    LOG(ERROR) << "Failed to unwrap handle";
    return base::ScopedPlatformFile();
  }
  return fd;
}

}  // namespace diagnostics::mojo_utils
