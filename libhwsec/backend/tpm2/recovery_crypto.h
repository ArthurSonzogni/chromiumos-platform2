// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_RECOVERY_CRYPTO_H_
#define LIBHWSEC_BACKEND_TPM2_RECOVERY_CRYPTO_H_

#include <optional>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2;

class RecoveryCryptoTpm2 : public Backend::RecoveryCrypto,
                           public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<std::optional<brillo::SecureBlob>> GenerateKeyAuthValue();
  StatusOr<EncryptEccPrivateKeyResponse> EncryptEccPrivateKey(
      const EncryptEccPrivateKeyRequest& request);
  StatusOr<crypto::ScopedEC_POINT> GenerateDiffieHellmanSharedSecret(
      const GenerateDhSharedSecretRequest& request);
  StatusOr<std::optional<RecoveryCryptoRsaKeyPair>> GenerateRsaKeyPair();
  StatusOr<std::optional<brillo::Blob>> SignRequestPayload(
      const brillo::Blob& encrypted_rsa_private_key,
      const brillo::Blob& request_payload);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_RECOVERY_CRYPTO_H_
