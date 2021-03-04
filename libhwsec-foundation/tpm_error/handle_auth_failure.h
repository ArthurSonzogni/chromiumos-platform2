// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TPM_ERROR_HANDLE_AUTH_FAILURE_H_
#define LIBHWSEC_FOUNDATION_TPM_ERROR_HANDLE_AUTH_FAILURE_H_

#include "libhwsec-foundation/hwsec-foundation_export.h"
#include "libhwsec-foundation/tpm_error/tpm_error_data.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Handles the auth failure if necessary accordring to `data`, including
// resetting DA mitigation and error reporting.
HWSEC_FOUNDATION_EXPORT int HandleAuthFailure(const struct TpmErrorData* data);

#if defined(__cplusplus)
}
#endif

#endif  // LIBHWSEC_FOUNDATION_TPM_ERROR_HANDLE_AUTH_FAILURE_H_
