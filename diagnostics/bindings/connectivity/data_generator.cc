// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/bindings/connectivity/data_generator.h"

#include <fcntl.h>

#include <mojo/public/cpp/system/platform_handle.h>

namespace diagnostics {
namespace bindings {
namespace connectivity {

constexpr char kDevNull[] = "/dev/null";

::mojo::ScopedHandle HandleDataGenerator::Generate() {
  has_next_ = false;
  return mojo::WrapPlatformFile(
      base::ScopedPlatformFile(open(kDevNull, O_RDONLY)));
}

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics
