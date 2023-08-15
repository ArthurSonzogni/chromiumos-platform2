// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/optee-plugin/frontend_impl.h"

#include <optional>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/space.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

StatusOr<brillo::Blob> OpteePluginFrontendImpl::SendRawCommand(
    const brillo::Blob& command) const {
  return middleware_.CallSync<&Backend::Vendor::SendRawCommand>(command);
}

StatusOr<brillo::Blob> OpteePluginFrontendImpl::GetRootOfTrustCert() const {
  ASSIGN_OR_RETURN(bool is_ready,
                   middleware_.CallSync<&Backend::RoData::IsReady>(
                       RoSpace::kWidevineRootOfTrustCert),
                   _.WithStatus<TPMError>("NV space not ready"));
  if (!is_ready) {
    return MakeStatus<TPMError>("NV space not ready", TPMRetryAction::kNoRetry);
  }
  return middleware_.CallSync<&Backend::RoData::Read>(
      RoSpace::kWidevineRootOfTrustCert);
}

StatusOr<brillo::Blob> OpteePluginFrontendImpl::GetChipIdentifyKeyCert() const {
  ASSIGN_OR_RETURN(bool is_ready,
                   middleware_.CallSync<&Backend::RoData::IsReady>(
                       RoSpace::kChipIdentityKeyCert),
                   _.WithStatus<TPMError>("NV space not ready"));
  if (!is_ready) {
    return MakeStatus<TPMError>("NV space not ready", TPMRetryAction::kNoRetry);
  }
  return middleware_.CallSync<&Backend::RoData::Read>(
      RoSpace::kChipIdentityKeyCert);
}

}  // namespace hwsec
