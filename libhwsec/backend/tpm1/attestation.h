// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_ATTESTATION_H_
#define LIBHWSEC_BACKEND_TPM1_ATTESTATION_H_

#include <string>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>

#include "libhwsec/backend/attestation.h"
#include "libhwsec/backend/tpm1/config.h"
#include "libhwsec/backend/tpm1/key_management.h"
#include "libhwsec/backend/tpm1/tss_helper.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class AttestationTpm1 : public Attestation {
 public:
  AttestationTpm1(overalls::Overalls& overalls,
                  TssHelper& tss_helper,
                  ConfigTpm1& config,
                  KeyManagementTpm1& key_management)
      : overalls_(overalls),
        tss_helper_(tss_helper),
        config_(config),
        key_management_(key_management) {}

  StatusOr<attestation::Quote> Quote(DeviceConfigs device_configs,
                                     Key key) override;
  StatusOr<bool> IsQuoted(DeviceConfigs device_configs,
                          const attestation::Quote& quote) override;
  StatusOr<attestation::CertifiedKey> CreateCertifiedKey(
      Key identity_key,
      attestation::KeyType key_type,
      attestation::KeyUsage key_usage,
      KeyRestriction restriction,
      EndorsementAuth endorsement_auth,
      const std::string& external_data) override;

  StatusOr<CreateIdentityResult> CreateIdentity(
      attestation::KeyType key_type) override;

 private:
  // Certifies the |key| by the |identity_key| with |external_data|.
  StatusOr<CertifyKeyResult> CertifyKey(Key key,
                                        Key identity_key,
                                        const std::string& external_data);

  overalls::Overalls& overalls_;
  TssHelper& tss_helper_;
  ConfigTpm1& config_;
  KeyManagementTpm1& key_management_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_ATTESTATION_H_
