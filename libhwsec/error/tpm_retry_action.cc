// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/error/tpm_retry_action.h"

namespace hwsec {

const char* GetTPMRetryActionName(TPMRetryAction action) {
  switch (action) {
    case TPMRetryAction::kNone:
      return "kNone";
    case TPMRetryAction::kCommunication:
      return "kCommunication";
    case TPMRetryAction::kSession:
      return "kSession";
    case TPMRetryAction::kLater:
      return "kLater";
    case TPMRetryAction::kReboot:
      return "kReboot";
    case TPMRetryAction::kDefend:
      return "kDefend";
    case TPMRetryAction::kUserAuth:
      return "kUserAuth";
    case TPMRetryAction::kNoRetry:
      return "kNoRetry";
    case TPMRetryAction::kEllipticCurveScalarOutOfRange:
      return "kEllipticCurveScalarOutOfRange";
    case TPMRetryAction::kUserPresence:
      return "kUserPresence";
    case TPMRetryAction::kSpaceNotFound:
      return "kSpaceNotFound";
  }
  return "Invalid TPMRetryAction";
}

}  // namespace hwsec
