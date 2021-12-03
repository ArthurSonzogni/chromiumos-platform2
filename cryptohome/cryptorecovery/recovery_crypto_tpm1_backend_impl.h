// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM1_BACKEND_IMPL_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM1_BACKEND_IMPL_H_

#include <map>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bn.h>
#include <openssl/ec.h>

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/cryptorecovery/recovery_crypto.h"

namespace cryptohome {

class TpmImpl;

namespace cryptorecovery {

// Implements the recovery crypto backend for devices with TPM 1.2, which
// requires AP's elliptic-curve support.
class RecoveryCryptoTpm1BackendImpl final : public RecoveryCryptoTpmBackend {
 public:
  explicit RecoveryCryptoTpm1BackendImpl(TpmImpl* tpm_impl);
  RecoveryCryptoTpm1BackendImpl(const RecoveryCryptoTpm1BackendImpl&) = delete;
  RecoveryCryptoTpm1BackendImpl& operator=(
      const RecoveryCryptoTpm1BackendImpl&) = delete;
  ~RecoveryCryptoTpm1BackendImpl() override;

  // Generate key_auth_value. key auth value is required for sealing/ unsealing
  // in TPM1.2 only and the required length is 32 bytes.
  brillo::SecureBlob GenerateKeyAuthValue() override;
  // Performs the encryption by sealing the supplied crypto secret via the
  // TPM_Seal command.
  bool EncryptEccPrivateKey(
      const EllipticCurve& ec,
      const crypto::ScopedEC_KEY& own_key_pair,
      const base::Optional<brillo::SecureBlob>& auth_value,
      brillo::SecureBlob* encrypted_own_priv_key) override;
  // Performs the scalar multiplication by unsealing the encrypted secret via
  // the TPM_Unseal command and generated the corresponding shared secret via
  // ECDH_HKDF.
  crypto::ScopedEC_POINT GenerateDiffieHellmanSharedSecret(
      const EllipticCurve& ec,
      const brillo::SecureBlob& encrypted_own_priv_key,
      const base::Optional<brillo::SecureBlob>& auth_value,
      const EC_POINT& others_pub_point) override;

 private:
  TpmImpl* const tpm_impl_;
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM1_BACKEND_IMPL_H_
