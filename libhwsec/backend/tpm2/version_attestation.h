// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_VERSION_ATTESTATION_H_
#define LIBHWSEC_BACKEND_TPM2_VERSION_ATTESTATION_H_

#include <string>
#include <utility>

#include "libhwsec/backend/tpm2/key_management.h"
#include "libhwsec/backend/tpm2/trunks_context.h"
#include "libhwsec/backend/version_attestation.h"
#include "libhwsec/platform/platform.h"
#include "libhwsec/status.h"

namespace hwsec {

class VersionAttestationTpm2 : public VersionAttestation {
 public:
  VersionAttestationTpm2(TrunksContext& context,
                         ConfigTpm2& config,
                         KeyManagementTpm2& key_management,
                         Platform& platform)
      : context_(context),
        config_(config),
        key_management_(key_management),
        platform_(platform) {}

  StatusOr<arc_attestation::CrOSVersionAttestationBlob> AttestVersion(
      Key key, const std::string& cert, const brillo::Blob& challenge) override;

 private:
  struct PcrQuoteResult {
    std::string signature;
    std::string quoted;
  };

  TrunksContext& context_;
  ConfigTpm2& config_;
  KeyManagementTpm2& key_management_;
  Platform& platform_;

  StatusOr<PcrQuoteResult> QuotePCRInternal(Key& key,
                                            const std::string& challenge);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_VERSION_ATTESTATION_H_
