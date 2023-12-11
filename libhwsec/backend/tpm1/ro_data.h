// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_RO_DATA_H_
#define LIBHWSEC_BACKEND_TPM1_RO_DATA_H_

#include <cstdint>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <brillo/secure_blob.h>

#include "libhwsec/backend/ro_data.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class RoDataTpm1 : public RoData {
 public:
  explicit RoDataTpm1(org::chromium::TpmNvramProxyInterface& tpm_nvram)
      : tpm_nvram_(tpm_nvram) {}

  StatusOr<bool> IsReady(RoSpace space) override;
  StatusOr<brillo::Blob> Read(RoSpace space) override;
  StatusOr<attestation::Quote> Certify(RoSpace space, Key key) override;

 private:
  org::chromium::TpmNvramProxyInterface& tpm_nvram_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_RO_DATA_H_
