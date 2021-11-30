// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_IMPL_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_IMPL_H_

#include <memory>

#include <brillo/secure_blob.h>

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/cryptorecovery/recovery_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {
// Cryptographic operations for cryptohome recovery performed on either CPU
// (software emulation) or TPM modules depending on the TPM backend.
class RecoveryCryptoImpl : public RecoveryCrypto {
 public:
  // Creates instance. Returns nullptr if error occurred.
  static std::unique_ptr<RecoveryCryptoImpl> Create(
      RecoveryCryptoTpmBackend* tpm_backend);

  RecoveryCryptoImpl(const RecoveryCryptoImpl&) = delete;
  RecoveryCryptoImpl& operator=(const RecoveryCryptoImpl&) = delete;

  ~RecoveryCryptoImpl() override;

  bool GenerateRecoveryRequest(
      const HsmPayload& hsm_payload,
      const RequestMetadata& request_meta_data,
      const brillo::SecureBlob& encrypted_channel_priv_key,
      const brillo::SecureBlob& channel_pub_key,
      const brillo::SecureBlob& epoch_pub_key,
      brillo::SecureBlob* recovery_request,
      brillo::SecureBlob* ephemeral_pub_key) const override;
  bool GenerateHsmPayload(
      const brillo::SecureBlob& mediator_pub_key,
      const brillo::SecureBlob& rsa_pub_key,
      const OnboardingMetadata& onboarding_metadata,
      HsmPayload* hsm_payload,
      brillo::SecureBlob* encrypted_destination_share,
      brillo::SecureBlob* recovery_key,
      brillo::SecureBlob* channel_pub_key,
      brillo::SecureBlob* encrypted_channel_priv_key) const override;
  bool RecoverDestination(const brillo::SecureBlob& dealer_pub_key,
                          const brillo::SecureBlob& encrypted_destination_share,
                          const brillo::SecureBlob& ephemeral_pub_key,
                          const brillo::SecureBlob& mediated_publisher_pub_key,
                          brillo::SecureBlob* destination_dh) const override;
  bool DecryptResponsePayload(
      const brillo::SecureBlob& encrypted_channel_priv_key,
      const brillo::SecureBlob& epoch_pub_key,
      const brillo::SecureBlob& recovery_response_cbor,
      HsmResponsePlainText* response_plain_text) const override;

 private:
  RecoveryCryptoImpl(EllipticCurve ec, RecoveryCryptoTpmBackend* tpm_backend);
  // Encrypts mediator share and stores as `encrypted_ms` with
  // embedded ephemeral public key, AES-GCM tag and iv. Returns false if error
  // occurred.
  bool EncryptMediatorShare(const brillo::SecureBlob& mediator_pub_key,
                            const brillo::SecureBlob& mediator_share,
                            EncryptedMediatorShare* encrypted_ms,
                            BN_CTX* context) const;
  bool GenerateRecoveryKey(const crypto::ScopedEC_POINT& recovery_pub_point,
                           const crypto::ScopedEC_KEY& dealer_key_pair,
                           brillo::SecureBlob* recovery_key) const;
  // Generate ephemeral public and inverse public keys {G*x, G*-x}
  bool GenerateEphemeralKey(brillo::SecureBlob* ephemeral_pub_key,
                            brillo::SecureBlob* ephemeral_inv_pub_key) const;
  bool GenerateHsmAssociatedData(const brillo::SecureBlob& channel_pub_key,
                                 const brillo::SecureBlob& rsa_pub_key,
                                 const OnboardingMetadata& onboarding_metadata,
                                 brillo::SecureBlob* hsm_associated_data,
                                 brillo::SecureBlob* publisher_priv_key,
                                 brillo::SecureBlob* publisher_pub_key) const;

  EllipticCurve ec_;
  RecoveryCryptoTpmBackend* const tpm_backend_;
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_IMPL_H_
