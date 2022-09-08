// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_RECOVERY_CRYPTO_MOCK_FRONTEND_H_
#define LIBHWSEC_FRONTEND_RECOVERY_CRYPTO_MOCK_FRONTEND_H_

#include <optional>
#include <vector>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/frontend/mock_frontend.h"
#include "libhwsec/frontend/recovery_crypto/frontend.h"

namespace hwsec {

class MockRecoveryCryptoFrontend : public MockFrontend,
                                   public RecoveryCryptoFrontend {
 public:
  MockRecoveryCryptoFrontend() = default;
  ~MockRecoveryCryptoFrontend() override = default;

  MOCK_METHOD(StatusOr<std::optional<brillo::SecureBlob>>,
              GenerateKeyAuthValue,
              (),
              (override));
  MOCK_METHOD(StatusOr<EncryptEccPrivateKeyResponse>,
              EncryptEccPrivateKey,
              (EncryptEccPrivateKeyRequest request),
              (override));
  MOCK_METHOD(StatusOr<crypto::ScopedEC_POINT>,
              GenerateDiffieHellmanSharedSecret,
              (GenerateDhSharedSecretRequest request),
              (override));
  MOCK_METHOD(StatusOr<std::optional<RecoveryCryptoRsaKeyPair>>,
              GenerateRsaKeyPair,
              (),
              (override));
  MOCK_METHOD(StatusOr<std::optional<brillo::Blob>>,
              SignRequestPayload,
              (const brillo::Blob& encrypted_rsa_private_key,
               const brillo::Blob& request_payload),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_RECOVERY_CRYPTO_MOCK_FRONTEND_H_
