// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <trousers/trousers.h>
#include <base/strings/stringprintf.h>

#include "libhwsec/error/tpm1_error.h"

namespace {

std::string FormatTrousersErrorCode(TSS_RESULT result) {
  return base::StringPrintf("TPM error 0x%x (%s)", result,
                            Trspi_Error_String(result));
}

}  // namespace

namespace hwsec {
namespace error {

std::string TPM1ErrorObj::ToReadableString() const {
  return FormatTrousersErrorCode(error_code_);
}

hwsec_foundation::error::ErrorBase TPM1ErrorObj::SelfCopy() const {
  return std::make_unique<TPM1ErrorObj>(error_code_);
}

TPMRetryAction TPM1ErrorObj::ToTPMRetryAction() const {
  TPMRetryAction status = TPMRetryAction::kNoRetry;
  switch (ERROR_CODE(error_code_)) {
    case ERROR_CODE(TSS_SUCCESS):
      status = TPMRetryAction::kNone;
      break;
    // Communications failure with the TPM.
    case ERROR_CODE(TSS_E_COMM_FAILURE):
      status = TPMRetryAction::kCommunication;
      break;
    // Invalid handle to the TPM.
    case ERROR_CODE(TSS_E_INVALID_HANDLE):
      status = TPMRetryAction::kLater;
      break;
    // Key load failed; problem with parent key authorization.
    case ERROR_CODE(TCS_E_KM_LOADFAILED):
      status = TPMRetryAction::kLater;
      break;
    // The TPM is defending itself against possible dictionary attacks.
    case ERROR_CODE(TPM_E_DEFEND_LOCK_RUNNING):
      status = TPMRetryAction::kDefend;
      break;
    // TPM is out of memory, a reboot is needed.
    case ERROR_CODE(TPM_E_SIZE):
      status = TPMRetryAction::kReboot;
      break;
    // The TPM returned TPM_E_FAIL. A reboot is required.
    case ERROR_CODE(TPM_E_FAIL):
      status = TPMRetryAction::kReboot;
      break;
    // Retrying will not help.
    default:
      status = TPMRetryAction::kNoRetry;
      break;
  }
  return status;
}

}  // namespace error
}  // namespace hwsec
