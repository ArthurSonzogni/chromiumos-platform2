// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_ATTESTATION_H_
#define LIBHWSEC_BACKEND_TPM2_ATTESTATION_H_

#include "libhwsec/backend/attestation.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/attestation.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class AttestationTpm2 : public Attestation {
 public:
  AttestationTpm2() {}

  StatusOr<Quotation> Quote(DeviceConfigs device_configs, Key key) override;
  StatusOr<bool> IsQuoted(DeviceConfigs device_configs,
                          const Quotation& quotation) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_ATTESTATION_H_
