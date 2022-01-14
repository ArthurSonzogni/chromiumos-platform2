// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_FAKE_TPM_BACKEND_IMPL_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_FAKE_TPM_BACKEND_IMPL_H_

#include <optional>

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

  // Generate key_auth_value. key auth value is not required for fake TPM
  // and therefore, an empty SecureBlob will be returned.
  brillo::SecureBlob GenerateKeyAuthValue() override;
  // Returns the raw ECC private key (without any encryption). auth_value will
  // not be used as it's to seal the private key on TPM1 modules when ECC
  // operations are not supported.
  bool EncryptEccPrivateKey(
      const EllipticCurve& ec,
      const crypto::ScopedEC_KEY& own_key_pair,
      const std::optional<brillo::SecureBlob>& /*auth_value*/,
      brillo::SecureBlob* encrypted_own_priv_key) override;
  // Performs the scalar multiplication of the raw private key and the
  // supplied point in software. auth_value will not be used as it's to seal
  // the private key on TPM1 modules when ECC operations are not supported.
  crypto::ScopedEC_POINT GenerateDiffieHellmanSharedSecret(
      const EllipticCurve& ec,
      const brillo::SecureBlob& encrypted_own_priv_key,
      const std::optional<brillo::SecureBlob>& /*auth_value*/,
      const EC_POINT& others_pub_point) override;
  // Generate RSA key pair from tpm modules. Return true if the key generation
  // from TPM modules is successful.
  // TODO(b:196191918): implement the function for testing
  bool GenerateRsaKeyPair(
      brillo::SecureBlob* /*encrypted_rsa_private_key*/,
      brillo::SecureBlob* /*rsa_public_key_spki_der*/) override;
  // Sign the request payload with the provided RSA private key. Return true if
  // the signing operation is successful.
  // TODO(b:196191918): implement the function for testing
  bool SignRequestPayload(
      const brillo::SecureBlob& /*encrypted_rsa_private_key*/,
      const brillo::SecureBlob& /*request_payload*/,
      brillo::SecureBlob* /*signature*/) override;
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_FAKE_TPM_BACKEND_IMPL_H_
