// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_RECOVERY_CRYPTO_H_
#define LIBHWSEC_BACKEND_MOCK_RECOVERY_CRYPTO_H_

#include <optional>
#include <vector>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/recovery_crypto.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/no_default_init.h"
#include "libhwsec/structures/operation_policy.h"
#include "libhwsec-foundation/crypto/elliptic_curve.h"

namespace hwsec {

class MockRecoveryCrypto : public RecoveryCrypto {
 public:
  MOCK_METHOD(StatusOr<std::optional<brillo::SecureBlob>>,
              GenerateKeyAuthValue,
              (),
              (override));
  MOCK_METHOD(StatusOr<EncryptEccPrivateKeyResponse>,
              EncryptEccPrivateKey,
              (const EncryptEccPrivateKeyRequest& request),
              (override));
  MOCK_METHOD(StatusOr<crypto::ScopedEC_POINT>,
              GenerateDiffieHellmanSharedSecret,
              (const GenerateDhSharedSecretRequest& request),
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

#endif  // LIBHWSEC_BACKEND_MOCK_RECOVERY_CRYPTO_H_
