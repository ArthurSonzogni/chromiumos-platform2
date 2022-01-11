// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM2_BACKEND_IMPL_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM2_BACKEND_IMPL_H_

#include <optional>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bn.h>
#include <openssl/ec.h>

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/cryptorecovery/recovery_crypto.h"

namespace cryptohome {

class Tpm2Impl;

namespace cryptorecovery {

// Implements the recovery crypto backend for devices with TPM 2.0, using the
// TPM's built-in elliptic-curve support.
class RecoveryCryptoTpm2BackendImpl final : public RecoveryCryptoTpmBackend {
 public:
  explicit RecoveryCryptoTpm2BackendImpl(Tpm2Impl* tpm2_impl);
  RecoveryCryptoTpm2BackendImpl(const RecoveryCryptoTpm2BackendImpl&) = delete;
  RecoveryCryptoTpm2BackendImpl& operator=(
      const RecoveryCryptoTpm2BackendImpl&) = delete;
  ~RecoveryCryptoTpm2BackendImpl() override;

  // Generate key_auth_value. key auth value is not required for in TPM2.0
  // and therefore, an empty SecureBlob will be returned.
  brillo::SecureBlob GenerateKeyAuthValue() override;
  // Performs the encryption by importing the supplied share via the TPM2_Import
  // command. auth_value will not be used as it's to seal the private key on
  // TPM1 modules when ECC operations are not supported.
  bool EncryptEccPrivateKey(
      const EllipticCurve& ec,
      const crypto::ScopedEC_KEY& own_key_pair,
      const std::optional<brillo::SecureBlob>& /*auth_value*/,
      brillo::SecureBlob* encrypted_own_priv_key) override;
  // Performs the scalar multiplication by loading the encrypted share via the
  // TPM2_Load command and multiplying it via the TPM2_ECDH_ZGen command.
  // auth_value will not be used as it's to seal the private key on TPM1
  // modules when ECC operations are not supported.
  crypto::ScopedEC_POINT GenerateDiffieHellmanSharedSecret(
      const EllipticCurve& ec,
      const brillo::SecureBlob& encrypted_own_priv_key,
      const std::optional<brillo::SecureBlob>& /*auth_value*/,
      const EC_POINT& others_pub_point) override;

 private:
  Tpm2Impl* const tpm2_impl_;
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM2_BACKEND_IMPL_H_
