// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_ACTION_H_
#define CRYPTOHOME_ERROR_ACTION_H_

#include <set>

namespace cryptohome {

namespace error {

enum class ErrorAction {
  // This entry is not used.
  kNull = 0,

  kCreateRequired = 301,
  kNotifyOldEncryption,
  kResumePreviousMigration,
  kTpmUpdateRequired,
  kTpmNeedsReboot,
  kTpmLockout,
  kIncorrectAuth,

  kRetry = 501,
  kReboot,
  kAuth,
  kDeleteVault,
  kPowerwash,
  kDevCheckUnexpectedState,
  kFatal
};

using ErrorActionSet = std::set<ErrorAction>;

inline ErrorActionSet NoErrorAction() {
  return {};
}

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_ACTION_H_
