// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/backend.h"

#include <algorithm>
#include <cstdint>

#include <trunks/tpm_utility.h>

extern "C" {
#define __packed __attribute((packed))
#define __aligned(x) __attribute((aligned(x)))
#include <trunks/cr50_headers/pinweaver_types.h>
}

#include "libhwsec/error/tpm2_error.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using PinWeaverTpm2 = BackendTpm2::PinWeaverTpm2;

StatusOr<bool> PinWeaverTpm2::IsEnabled() {
  return GetVersion().ok();
}

StatusOr<uint8_t> PinWeaverTpm2::GetVersion() {
  TrunksClientContext& context = backend_.trunks_context_;

  uint8_t version = 255;

  auto status = MakeStatus<TPM2Error>(
      context.tpm_utility->PinWeaverIsSupported(PW_PROTOCOL_VERSION, &version));

  if (!status.ok()) {
    if (status->ErrorCode() != trunks::SAPI_RC_ABI_MISMATCH) {
      return MakeStatus<TPMError>("Failed to check pinweaver support")
          .Wrap(std::move(status));
    }
    status = MakeStatus<TPM2Error>(
        context.tpm_utility->PinWeaverIsSupported(0, &version));
  }

  if (!status.ok()) {
    return MakeStatus<TPMError>("Failed to check pinweaver support")
        .Wrap(std::move(status));
  }

  return std::min(version, static_cast<uint8_t>(PW_PROTOCOL_VERSION));
}

StatusOr<brillo::Blob> PinWeaverTpm2::SendCommand(const brillo::Blob& command) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec
