// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtpmcrypto/tpm.h"

#include <libhwsec-foundation/tpm/tpm_version.h>

#if USE_TPM2
#include "libtpmcrypto/tpm2_impl.h"
#endif

#if USE_TPM1
#include "libtpmcrypto/tpm1_impl.h"
#endif

namespace tpmcrypto {

std::unique_ptr<Tpm> CreateTpmInstance() {
  TPM_SELECT_BEGIN;
  TPM2_SECTION({ return std::make_unique<Tpm2Impl>(); });
  TPM1_SECTION({ return std::make_unique<Tpm1Impl>(); });
  OTHER_TPM_SECTION();
  TPM_SELECT_END;
  return nullptr;
}

}  // namespace tpmcrypto
