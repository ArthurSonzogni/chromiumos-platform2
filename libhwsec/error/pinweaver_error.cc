// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/error/tpm_retry_action.h"

#include "libhwsec/error/pinweaver_error.h"
namespace {

using PinWeaverErrorCode = hwsec::PinWeaverError::PinWeaverErrorCode;

const char* PinweaverManagerStatusString(PinWeaverErrorCode error_code) {
  switch (error_code) {
    case PinWeaverErrorCode::kSuccess:
      return "kSuccess";
    case PinWeaverErrorCode::kExpired:
      return "kExpired";
    case PinWeaverErrorCode::kHashTreeOutOfSync:
      return "kHashTreeOutOfSync";
    case PinWeaverErrorCode::kInvalidLeSecret:
      return "kInvalidLeSecret";
    case PinWeaverErrorCode::kInvalidResetSecret:
      return "kInvalidResetSecret";
    case PinWeaverErrorCode::kPolicyNotMatch:
      return "kPolicyNotMatch";
    case PinWeaverErrorCode::kTooManyAttempts:
      return "kTooManyAttempts";
    case PinWeaverErrorCode::kOther:
      return "kOther";
  }
}

std::string FormatPinweaverManagerStatus(PinWeaverErrorCode result) {
  return base::StringPrintf("Pinweaver Manager Error Code %d (%s)",
                            static_cast<int>(result),
                            PinweaverManagerStatusString(result));
}

}  // namespace

namespace hwsec {

PinWeaverError::PinWeaverError(PinWeaverErrorCode error_code)
    : TPMErrorBase(FormatPinweaverManagerStatus(error_code)),
      error_code_(error_code) {}

TPMRetryAction PinWeaverError::ToTPMRetryAction() const {
  switch (error_code_) {
    case PinWeaverErrorCode::kSuccess:
      return TPMRetryAction::kNone;
    case PinWeaverErrorCode::kExpired:
      return TPMRetryAction::kPinWeaverExpired;
    case PinWeaverErrorCode::kHashTreeOutOfSync:
      return TPMRetryAction::kPinWeaverOutOfSync;
    case PinWeaverErrorCode::kInvalidLeSecret:
    case PinWeaverErrorCode::kInvalidResetSecret:
    case PinWeaverErrorCode::kPolicyNotMatch:
      return TPMRetryAction::kUserAuth;
    case PinWeaverErrorCode::kTooManyAttempts:
      return TPMRetryAction::kPinWeaverLockedOut;
    case PinWeaverErrorCode::kOther:
      return TPMRetryAction::kNoRetry;
  }
}

}  // namespace hwsec
