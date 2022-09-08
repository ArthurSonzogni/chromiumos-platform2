// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_ACTION_H_
#define CRYPTOHOME_ERROR_ACTION_H_

#include <set>

namespace cryptohome {

namespace error {

// Note that entries in ErrorAction may be logged in Cryptohome*Error classes,
// and as such should not be changed without removing the logging mentioned
// above.
enum class ErrorAction {
  // This entry is not used.
  kNull = 0,

  // The entries below are specific actions on the Chromium side. See
  // PrimaryAction enum in system_api/dbus/cryptohome/UserDataAuth.proto for
  // documentation on each of the enums below.
  kCreateRequired = 301,
  kNotifyOldEncryption,
  kResumePreviousMigration,
  kTpmUpdateRequired,
  kTpmNeedsReboot,
  kTpmLockout,
  kIncorrectAuth,

  // The entries below are generic possible resolution to an issue. See
  // PossibleAction enum in system_api/dbus/cryptohome/UserDataAuth.proto for
  // documentation on each of the enums below.
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
