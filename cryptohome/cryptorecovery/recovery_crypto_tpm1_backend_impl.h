// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM1_BACKEND_IMPL_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM1_BACKEND_IMPL_H_

#include <map>
#include <optional>
#include <string>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <openssl/bn.h>
#include <openssl/ec.h>

#include "cryptohome/cryptorecovery/recovery_crypto.h"
#include "cryptohome/tpm.h"

namespace cryptohome {

namespace cryptorecovery {

// Implements the recovery crypto backend for devices with TPM 1.2, which
// requires AP's elliptic-curve support.
class RecoveryCryptoTpm1BackendImpl final : public RecoveryCryptoTpmBackend {
 public:
  explicit RecoveryCryptoTpm1BackendImpl(Tpm* tpm_impl);
  RecoveryCryptoTpm1BackendImpl(const RecoveryCryptoTpm1BackendImpl&) = delete;
  RecoveryCryptoTpm1BackendImpl& operator=(
      const RecoveryCryptoTpm1BackendImpl&) = delete;
  ~RecoveryCryptoTpm1BackendImpl() override;

  // Generate key_auth_value. key auth value is required for sealing/ unsealing
  // in TPM1.2 only and the required length is 32 bytes.
  brillo::SecureBlob GenerateKeyAuthValue() override;
  // Performs the encryption by sealing the supplied crypto secret via the
  // TPM_Seal command.
  bool EncryptEccPrivateKey(const EncryptEccPrivateKeyRequest& request,
                            EncryptEccPrivateKeyResponse* response) override;
  // Performs the scalar multiplication by unsealing the encrypted secret via
  // the TPM_Unseal command and generated the corresponding shared secret via
  // ECDH_HKDF.
  crypto::ScopedEC_POINT GenerateDiffieHellmanSharedSecret(
      const GenerateDhSharedSecretRequest& request) override;
  // Generate RSA key pair from tpm modules. Return true if the key generation
  // from TPM modules is successful.
  // Generated RSA private key would be used to sign recovery request payload
  // when channel private key cannot be restored in a secure manner. Therefore,
  // it will only be implemented in TPM1 backend.
  bool GenerateRsaKeyPair(brillo::SecureBlob* encrypted_rsa_private_key,
                          brillo::SecureBlob* rsa_public_key_spki_der) override;
  // Sign the request payload with the provided RSA private key. Return true if
  // the signing operation is successful.
  // The RSA private key would be loaded from the TPM modules first and used to
  // sign the payload.
  bool SignRequestPayload(const brillo::SecureBlob& encrypted_rsa_private_key,
                          const brillo::SecureBlob& request_payload,
                          brillo::SecureBlob* signature) override;

 private:
  Tpm* const tpm_impl_;
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_TPM1_BACKEND_IMPL_H_
