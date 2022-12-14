// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/oobe_config/frontend_impl.h"

#include <optional>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

Status OobeConfigFrontendImpl::IsRollbackSpaceReady() {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

Status OobeConfigFrontendImpl::ResetRollbackSpace() {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

StatusOr<brillo::Blob> OobeConfigFrontendImpl::Encrypt(
    const brillo::SecureBlob& plain_data) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

StatusOr<brillo::SecureBlob> OobeConfigFrontendImpl::Decrypt(
    const brillo::Blob& encrypted_data) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec
