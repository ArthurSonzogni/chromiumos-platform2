// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/attestation.h"

#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/status.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

StatusOr<Quotation> AttestationTpm1::Quote(DeviceConfigs device_configs,
                                           Key key) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

StatusOr<bool> AttestationTpm1::IsQuoted(DeviceConfigs device_configs,
                                         const Quotation& quotation) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec
