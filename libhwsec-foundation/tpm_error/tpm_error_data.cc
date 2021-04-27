// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/tpm_error_data.h"

#if defined(__cplusplus)

bool operator==(const struct TpmErrorData& a, const struct TpmErrorData& b) {
  return a.command == b.command && a.response == b.response;
}

bool operator<(const struct TpmErrorData& a, const struct TpmErrorData& b) {
  return a.command < b.command ? true : a.response < b.response;
}

#endif  // defined(__cplusplus)
