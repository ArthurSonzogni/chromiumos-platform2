// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_STRUCTURES_PERMISSION_H_
#define LIBHWSEC_STRUCTURES_PERMISSION_H_

#include <optional>

#include <brillo/secure_blob.h>

#include "libhwsec/structures/device_config.h"

namespace hwsec {

struct Permission {
  std::optional<brillo::SecureBlob> auth_value;
};

}  // namespace hwsec

#endif  // LIBHWSEC_STRUCTURES_PERMISSION_H_
