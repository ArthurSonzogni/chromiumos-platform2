// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM_RETRY_ACTION_H_
#define LIBHWSEC_ERROR_TPM_RETRY_ACTION_H_

namespace hwsec {
namespace error {

enum class TPMRetryAction {
  // Action succeeded - Succeeded.
  // Recommended action: No further action needed.
  kNone,

  // Action failed - Communication failure.
  // Recommended action: Uses exponential retry. After exceeding the retry
  // limit, it should become kLater.
  kCommunication,

  // Action failed - Session failure.
  // Recommended action: Refreshes the session. After exceeding the retry limit,
  // it should become kLater.
  kSession,

  // Action failed - Retry the action later.
  // Recommended action: Retries after reloading the handlers. After exceeding
  // the retry limit, it should become kReboot.
  kLater,

  // Action failed - The state that requires reboot.
  // Recommended action: Asks the user to reboot the machine.
  kReboot,

  // Action failed - In the defense mode.
  // Recommended action: Tells the user that they need to wait until it unlock.
  kDefend,

  // Action failed - User authorization failure.
  // Recommended action: Informs the user that they used the wrong
  // authorization.
  kUserAuth,

  // Action failed - Retrying won't change the outcome.
  // Recommended action: The upper layer should know what to do. And handles it
  // correctly.
  kNoRetry,
};

}  // namespace error
}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM_RETRY_ACTION_H_
