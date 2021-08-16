// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_allow_list_impl.h"

#include <base/check.h>

namespace tpm_manager {

TpmAllowListImpl::TpmAllowListImpl(TpmStatus* tpm_status)
    : tpm_status_(tpm_status) {
  CHECK(tpm_status_);
}

bool TpmAllowListImpl::IsAllowed() {
#if !USE_TPM_DYNAMIC
  // Allow all kinds of TPM if we are not using runtime TPM selection.
  return true;
#else
  return false;
#endif
}

}  // namespace tpm_manager
