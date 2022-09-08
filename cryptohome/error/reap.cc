// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/error/reap.h"

#include <base/logging.h>

namespace cryptohome {

namespace error {

void ReapWorkingAsIntendedError(CryptohomeStatus status) {
  if (status.ok()) {
    // No action required for OK status.
    return;
  }

  LOG(INFO) << "Expected error: " << status;
}

void ReapRetryError(CryptohomeStatus status) {
  if (status.ok()) {
    // No action required for OK status.
    return;
  }

  LOG(WARNING) << "This error caused a retry: " << status;
}

}  // namespace error

}  // namespace cryptohome
