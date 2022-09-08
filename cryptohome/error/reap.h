// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_REAP_H_
#define CRYPTOHOME_ERROR_REAP_H_

#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

// This function should be called when there's an error that is deemed to be
// working as intended. The error's ownership should be transferred into this
// function to be disposed of.
void ReapWorkingAsIntendedError(CryptohomeStatus status);

// This function should be called when there's an error that triggered a retry,
// and thus will not be propagated up the dbus stack. The error's ownership
// should be transferred into this function to be disposed of.
void ReapRetryError(CryptohomeStatus status);

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_REAP_H_
