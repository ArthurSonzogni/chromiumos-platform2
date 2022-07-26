// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_SIGNATURE_SEALING_H_
#define LIBHWSEC_BACKEND_TPM2_SIGNATURE_SEALING_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <trunks/trunks_factory.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/no_default_init.h"

namespace hwsec {

class BackendTpm2;

class SignatureSealingTpm2 : public Backend::SignatureSealing,
                             public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<SignatureSealedData> Seal(
      const std::vector<OperationPolicySetting>& policies,
      const brillo::SecureBlob& unsealed_data,
      const brillo::Blob& public_key_spki_der,
      const std::vector<Algorithm>& key_algorithms) override;
  StatusOr<ChallengeResult> Challenge(
      const OperationPolicy& policy,
      const SignatureSealedData& sealed_data,
      const brillo::Blob& public_key_spki_der,
      const std::vector<Algorithm>& key_algorithms) override;
  StatusOr<brillo::SecureBlob> Unseal(
      ChallengeID challenge, const brillo::Blob& challenge_response) override;

 private:
  struct InternalChallengeData {
    NoDefault<ChallengeID> challenge_id;
    brillo::Blob srk_wrapped_secret;
    brillo::Blob public_key_spki_der;
    trunks::TPM_ALG_ID scheme;
    trunks::TPM_ALG_ID hash_alg;
    std::unique_ptr<trunks::PolicySession> session;
    std::string session_nonce;
  };

  std::optional<InternalChallengeData> current_challenge_data_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_SIGNATURE_SEALING_H_
