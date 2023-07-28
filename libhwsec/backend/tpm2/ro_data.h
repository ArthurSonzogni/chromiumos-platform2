// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_RO_DATA_H_
#define LIBHWSEC_BACKEND_TPM2_RO_DATA_H_

#include <cstdint>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <brillo/secure_blob.h>

#include "libhwsec/backend/ro_data.h"
#include "libhwsec/backend/tpm2/key_management.h"
#include "libhwsec/backend/tpm2/signing.h"
#include "libhwsec/backend/tpm2/trunks_context.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class RoDataTpm2 : public RoData {
 public:
  explicit RoDataTpm2(TrunksContext& context,
                      KeyManagementTpm2& key_management,
                      SigningTpm2& signing,
                      org::chromium::TpmNvramProxyInterface& tpm_nvram)
      : context_(context),
        key_management_(key_management),
        signing_(signing),
        tpm_nvram_(tpm_nvram) {}

  StatusOr<bool> IsReady(RoSpace space) override;
  StatusOr<brillo::Blob> Read(RoSpace space) override;
  StatusOr<attestation::Quote> Certify(RoSpace space, Key key) override;
  StatusOr<attestation::Quote> CertifyWithSize(RoSpace space,
                                               Key key,
                                               int size) override;

 private:
  TrunksContext& context_;
  KeyManagementTpm2& key_management_;
  SigningTpm2& signing_;
  org::chromium::TpmNvramProxyInterface& tpm_nvram_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_RO_DATA_H_
