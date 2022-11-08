// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_RECOVERY_CRYPTO_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_RECOVERY_CRYPTO_FRONTEND_IMPL_H_

#include <optional>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/frontend/recovery_crypto/frontend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class RecoveryCryptoFrontendImpl : public RecoveryCryptoFrontend,
                                   public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~RecoveryCryptoFrontendImpl() override = default;

  StatusOr<std::optional<brillo::SecureBlob>> GenerateKeyAuthValue() override;
  StatusOr<EncryptEccPrivateKeyResponse> EncryptEccPrivateKey(
      EncryptEccPrivateKeyRequest request) override;
  StatusOr<crypto::ScopedEC_POINT> GenerateDiffieHellmanSharedSecret(
      GenerateDhSharedSecretRequest request) override;
  StatusOr<std::optional<RecoveryCryptoRsaKeyPair>> GenerateRsaKeyPair()
      override;
  StatusOr<std::optional<brillo::Blob>> SignRequestPayload(
      const brillo::Blob& encrypted_rsa_private_key,
      const brillo::Blob& request_payload) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_RECOVERY_CRYPTO_FRONTEND_IMPL_H_
