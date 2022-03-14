// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_LOCATIONS_H_
#define CRYPTOHOME_ERROR_LOCATIONS_H_

#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

// This file defines the various location code used by CryptohomeError
// Each of the location should only be used in one error site.

// This file is generated and maintained by the cryptohome/error/location_db.py
// utility. Please run this command in cros_sdk to update this file:
// $ /mnt/host/source/src/platform2/cryptohome/error/tool/location_db.py
//       --update

// Note that we should prevent the implicit cast of this enum class to
// ErrorLocation so that if the macro is not used, the compiler will catch it.
enum class ErrorLocationSpecifier : CryptohomeError::ErrorLocation {
  // Start of generated content. Do NOT modify after this line.
  // End of generated content.
};

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_LOCATIONS_H_
