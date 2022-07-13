// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM2_IMPL_H_
#define CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM2_IMPL_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <libhwsec/status.h>

#include "cryptohome/signature_sealing_backend.h"

namespace cryptohome {

class Tpm2Impl;

// Implementation of signature-sealing operations for TPM 2.0. Based on the
// TPM2_PolicySigned functionality.
class SignatureSealingBackendTpm2Impl final : public SignatureSealingBackend {
 public:
  explicit SignatureSealingBackendTpm2Impl(Tpm2Impl* tpm);
  SignatureSealingBackendTpm2Impl(const SignatureSealingBackendTpm2Impl&) =
      delete;
  SignatureSealingBackendTpm2Impl& operator=(
      const SignatureSealingBackendTpm2Impl&) = delete;

  ~SignatureSealingBackendTpm2Impl() override;

  // SignatureSealingBackend:
  hwsec::Status CreateSealedSecret(
      const brillo::Blob& public_key_spki_der,
      const std::vector<structure::ChallengeSignatureAlgorithm>& key_algorithms,
      const std::string& obfuscated_username,
      brillo::SecureBlob* secret_value,
      hwsec::SignatureSealedData* sealed_secret_data) override;
  hwsec::Status CreateUnsealingSession(
      const hwsec::SignatureSealedData& sealed_secret_data,
      const brillo::Blob& public_key_spki_der,
      const std::vector<structure::ChallengeSignatureAlgorithm>& key_algorithms,
      const std::set<uint32_t>& pcr_set,
      bool locked_to_single_user,
      std::unique_ptr<UnsealingSession>* unsealing_session) override;

 private:
  // Unowned.
  Tpm2Impl* const tpm_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM2_IMPL_H_
