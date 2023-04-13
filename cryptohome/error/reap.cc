// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/error/converter.h"
#include "cryptohome/error/reap.h"

#include <string>

#include <base/logging.h>

namespace cryptohome {

namespace error {

void ReapAndReportError(CryptohomeStatus status,
                        std::string error_bucket_name) {
  if (status.ok()) {
    // No action required for OK status.
    return;
  }

  user_data_auth::CryptohomeErrorCode legacy_ec;
  auto info = CryptohomeErrorToUserDataAuthError(status, &legacy_ec);
  ReportCryptohomeError(status, info, error_bucket_name);
}

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
