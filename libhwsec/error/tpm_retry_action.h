// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM_RETRY_ACTION_H_
#define LIBHWSEC_ERROR_TPM_RETRY_ACTION_H_

#include "libhwsec/hwsec_export.h"

namespace hwsec {

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
  // Recommended action: The upper layer should know what to do and handles it
  // correctly.
  kNoRetry,

  // Action failed - The elliptic curve scalar out of range.
  // Recommended action: Retry the operation with different scalar.
  kEllipticCurveScalarOutOfRange,

  // Action failed - User presence not detected.
  // Recommended action: Informs the user that they need to provide user
  // presence.
  kUserPresence,

  // Action failed - The space for this operation is not found. This includes
  // errors when retrieving labels for the pinweaver manager backend.
  // Recommended action: The upper layer should know what to do and handles it
  // correctly.
  kSpaceNotFound,

  // Action failed - PinWeaver operation failed due to credential expired.
  // Recommended action: The upper layer should know what to do and handles it
  // correctly.
  kPinWeaverExpired,

  // Action failed - PinWeaver operation failed due to too many attempts as per
  // delay schedule.
  // Recommended action: The upper layer should know what to do and handles it
  // correctly.
  kPinWeaverLockedOut,

  // Action failed - PinWeaver operation failed due to out-of-sync root hash.
  // Recommended action: Sync the root hash. Perform log replay if needed.
  kPinWeaverOutOfSync,

  // The max value should equal to the last item.
  kMaxValue = kPinWeaverOutOfSync,
};

// Gets the name of retry action.
const char* HWSEC_EXPORT GetTPMRetryActionName(TPMRetryAction action);

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM_RETRY_ACTION_H_
