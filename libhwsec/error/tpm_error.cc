// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include "libhwsec/error/tpm_error.h"
#include "libhwsec/error/tpm_retry_action.h"

namespace hwsec {

// See the comment in "libhwsec-foundation/status/impl/error.h" for why we need
// these external definitions.
TPMErrorBase::TPMErrorBase(std::string message) : Error(message) {}
TPMError::TPMError(std::string error_message, TPMRetryAction action)
    : TPMErrorBase(error_message), retry_action_(action) {}

}  // namespace hwsec
