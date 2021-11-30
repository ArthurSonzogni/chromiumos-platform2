// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/strings/stringprintf.h>

#include "libhwsec/error/tpm2_error.h"

namespace {

// The upper bits that identify the layer that produced the response.
// These bits are always 0 for the hardware TPM response codes.
constexpr trunks::TPM_RC kResponseLayerMask = 0xFFFFF000;

std::string FormatTrunksErrorCode(trunks::TPM_RC result) {
  return base::StringPrintf("TPM2 error 0x%x (%s)", result,
                            trunks::GetErrorString(result).c_str());
}

}  // namespace

namespace hwsec {

TPM2Error::TPM2Error(trunks::TPM_RC error_code)
    : TPMErrorBase(FormatTrunksErrorCode(error_code)),
      error_code_(error_code) {}

TPMRetryAction TPM2Error::ToTPMRetryAction() const {
  trunks::TPM_RC error_code = error_code_;
  // For hardware TPM errors and TPM-equivalent response codes produced by
  // Resource Manager, use just the error number and strip everything else.
  if ((error_code & kResponseLayerMask) == 0 ||
      (error_code & kResponseLayerMask) ==
          trunks::kResourceManagerTpmErrorBase) {
    error_code = trunks::GetFormatOneError(error_code & ~kResponseLayerMask);
  }
  TPMRetryAction status = TPMRetryAction::kNoRetry;
  switch (error_code) {
    case trunks::TPM_RC_SUCCESS:
      status = TPMRetryAction::kNone;
      break;
    // Communications failure with the TPM.
    case trunks::TRUNKS_RC_WRITE_ERROR:
    case trunks::TRUNKS_RC_READ_ERROR:
    case trunks::SAPI_RC_NO_CONNECTION:
    case trunks::SAPI_RC_NO_RESPONSE_RECEIVED:
    case trunks::SAPI_RC_MALFORMED_RESPONSE:
      status = TPMRetryAction::kCommunication;
      break;
    // Invalid handle to the TPM.
    case trunks::TPM_RC_HANDLE:
    case trunks::TPM_RC_REFERENCE_H0:
    case trunks::TPM_RC_REFERENCE_H1:
    case trunks::TPM_RC_REFERENCE_H2:
    case trunks::TPM_RC_REFERENCE_H3:
    case trunks::TPM_RC_REFERENCE_H4:
    case trunks::TPM_RC_REFERENCE_H5:
    case trunks::TPM_RC_REFERENCE_H6:
      status = TPMRetryAction::kLater;
      break;
    // The TPM is defending itself against possible dictionary attacks.
    case trunks::TPM_RC_LOCKOUT:
      status = TPMRetryAction::kDefend;
      break;
    // The TPM requires a reboot.
    case trunks::TPM_RC_INITIALIZE:
    case trunks::TPM_RC_REBOOT:
      status = TPMRetryAction::kReboot;
      break;
    // Retry the command later.
    case trunks::TPM_RC_RETRY:
    case trunks::TPM_RC_NV_RATE:
      status = TPMRetryAction::kLater;
      break;
    // Retrying will not help.
    default:
      status = TPMRetryAction::kNoRetry;
      break;
  }
  return status;
}

}  // namespace hwsec
