// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TPM_ERROR_TPM_ERROR_METRICS_CONSTANTS_H_
#define LIBHWSEC_FOUNDATION_TPM_ERROR_TPM_ERROR_METRICS_CONSTANTS_H_

namespace libhwsec_foundation {

// Used to report the command that causes auth failure.
constexpr char kTpm1AuthFailName[] = "Platform.TPM.AuthFailCommand.AuthFail";
// Used to report the command that causes 2nd auth failure.
constexpr char kTpm1Auth2FailName[] = "Platform.TPM.AuthFailCommand.Auth2Fail";

}  // namespace libhwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TPM_ERROR_TPM_ERROR_METRICS_CONSTANTS_H_
