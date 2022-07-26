// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_SIGNATURE_SEALING_H_
#define LIBHWSEC_BACKEND_TPM1_SIGNATURE_SEALING_H_

#include <optional>
#include <vector>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/no_default_init.h"

namespace hwsec {

class BackendTpm1;

class SignatureSealingTpm1 : public Backend::SignatureSealing,
                             public Backend::SubClassHelper<BackendTpm1> {
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

  const auto& get_current_challenge_data_for_test() const {
    return current_challenge_data_;
  }

 private:
  struct InternalChallengeData {
    NoDefault<ChallengeID> challenge_id;
    OperationPolicy policy;
    brillo::Blob srk_wrapped_cmk;
    brillo::Blob cmk_wrapped_auth_data;
    brillo::Blob pcr_bound_secret;
    brillo::Blob public_key_spki_der;
    brillo::Blob cmk_pubkey;
    brillo::Blob protection_key_pubkey;
    crypto::ScopedRSA migration_destination_rsa;
    brillo::Blob migration_destination_key_pubkey;
  };

  std::optional<InternalChallengeData> current_challenge_data_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_SIGNATURE_SEALING_H_
