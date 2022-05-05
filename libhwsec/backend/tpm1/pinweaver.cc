// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/backend.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using PinWeaverTpm1 = BackendTpm1::PinWeaverTpm1;

StatusOr<bool> PinWeaverTpm1::IsEnabled() {
  return false;
}

StatusOr<uint8_t> PinWeaverTpm1::GetVersion() {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<brillo::Blob> PinWeaverTpm1::SendCommand(const brillo::Blob& command) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec
