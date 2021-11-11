// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_FAKE_TPM_BACKEND_IMPL_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_FAKE_TPM_BACKEND_IMPL_H_

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bn.h>
#include <openssl/ec.h>

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/cryptorecovery/recovery_crypto.h"

namespace cryptohome {
namespace cryptorecovery {

// Implements the recovery crypto backend fully in software, without talking to
// the TPM. Should only be used when real-TPM-based backends aren't available.
class RecoveryCryptoFakeTpmBackendImpl final : public RecoveryCryptoTpmBackend {
 public:
  RecoveryCryptoFakeTpmBackendImpl();
  RecoveryCryptoFakeTpmBackendImpl(const RecoveryCryptoFakeTpmBackendImpl&) =
      delete;
  RecoveryCryptoFakeTpmBackendImpl& operator=(
      const RecoveryCryptoFakeTpmBackendImpl&) = delete;
  ~RecoveryCryptoFakeTpmBackendImpl() override;

  // Returns the raw ECC private key (without any encryption).
  bool EncryptEccPrivateKey(
      const EllipticCurve& ec,
      const BIGNUM& own_priv_key_bn,
      brillo::SecureBlob* encrypted_own_priv_key) override;
  // Performs the scalar multiplication of the raw private key and the
  // supplied point in software.
  crypto::ScopedEC_POINT GenerateDiffieHellmanSharedSecret(
      const EllipticCurve& ec,
      const brillo::SecureBlob& encrypted_own_priv_key,
      const EC_POINT& others_pub_point) override;
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_FAKE_TPM_BACKEND_IMPL_H_
