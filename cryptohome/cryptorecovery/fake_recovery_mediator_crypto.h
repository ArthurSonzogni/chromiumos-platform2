// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_FAKE_RECOVERY_MEDIATOR_CRYPTO_H_
#define CRYPTOHOME_CRYPTORECOVERY_FAKE_RECOVERY_MEDIATOR_CRYPTO_H_

#include <memory>

#include <brillo/secure_blob.h>

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/cryptorecovery/recovery_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {

// Cryptographic operations for fake mediator for cryptohome recovery.
// Recovery mechanism involves dealer, publisher, mediator and destination.
// The mediator is an external service that is invoked during the recovery
// process to perform mediation of an encrypted mediator share. The
// functionality of mediator should be implemented on the server and here it is
// implemented for testing purposes only.
class FakeRecoveryMediatorCrypto {
 public:
  // Creates instance. Returns nullptr if error occurred.
  static std::unique_ptr<FakeRecoveryMediatorCrypto> Create();

  // Returns hardcoded fake mediator public key for encrypting mediator share.
  // Do not use this key in production!
  // Returns false if error occurred.
  static bool GetFakeMediatorPublicKey(brillo::SecureBlob* mediator_pub_key);

  // Returns hardcoded fake mediator private key for decrypting mediator share.
  // Do not use this key in production!
  // Returns false if error occurred.
  static bool GetFakeMediatorPrivateKey(brillo::SecureBlob* mediator_priv_key);

  // Returns hardcoded fake epoch public key for encrypting request payload.
  // Do not use this key in production!
  // Returns false if error occurred.
  static bool GetFakeEpochPublicKey(brillo::SecureBlob* epoch_pub_key);

  // Returns hardcoded fake epoch private key for decrypting request payload.
  // Do not use this key in production!
  // Returns false if error occurred.
  static bool GetFakeEpochPrivateKey(brillo::SecureBlob* epoch_priv_key);

  // Performs mediation. Returns `mediated_publisher_pub_key`, which is
  // `publisher_pub_key` multiplied by secret `mediator_share` that only
  // mediator can decrypt from `encrypted_mediator_share`. Returns false if
  // error occurred. It is expected that `encrypted_mediator_share` is encrypted
  // to `mediator_priv_key`. Formula:
  //   mediator_share = Decrypt(encrypted_mediator_share)
  //   mediated_publisher_pub_key = publisher_pub_key * mediator_share
  bool Mediate(
      const brillo::SecureBlob& mediator_priv_key,
      const brillo::SecureBlob& publisher_pub_key,
      const RecoveryCrypto::EncryptedMediatorShare& encrypted_mediator_share,
      brillo::SecureBlob* mediated_publisher_pub_key) const;

  // Receives `request_payload`, performs mediation and generates response
  // payload. This function consist of the following steps:
  // 1. Deserialize `channel_pub_key` from `hsm_aead_ad` in
  // `request_payload.associated_data`.
  // 2. Perform DH(`epoch_priv_key`, channel_pub_key), decrypt
  // `cipher_text` (CT2) from `request_payload`.
  // 3. Extract `hsm_payload` from `request_payload`.
  // 4. Do `MediateHsmPayload` with `hsm_payload` and keys (`epoch_pub_key`,
  // `epoch_priv_key`, `mediator_priv_key`).
  bool MediateRequestPayload(const brillo::SecureBlob& epoch_pub_key,
                             const brillo::SecureBlob& epoch_priv_key,
                             const brillo::SecureBlob& mediator_priv_key,
                             const RequestPayload& request_payload,
                             ResponsePayload* response_payload) const;

 private:
  // Constructor is private. Use Create method to instantiate.
  explicit FakeRecoveryMediatorCrypto(EllipticCurve ec);

  // Receives `hsm_payload`, performs mediation and generates response payload.
  // This function consist of the following steps:
  // 1. Deserialize publisher_pub_key from `associated_data` in `hsm_payload`.
  // 2. Perform DH(`mediator_priv_key`, publisher_pub_key), decrypt
  // `cipher_text` from `hsm_payload` and get mediator_share and
  // dealer_pub_key
  // 3. Construct mediated_share = G * dealer_priv_key * mediator_share +
  // `ephemeral_pub_inv_key`.
  // 4. Serialize response payload associated_data and plain_text
  // 5. Generate encryption key as KDF(combine(epoch_pub_key,
  //                                     ECDH(epoch_priv_key, channel_pub_key)))
  // 6. Encrypt plain_text and generate `response_payload`
  bool MediateHsmPayload(const brillo::SecureBlob& mediator_priv_key,
                         const brillo::SecureBlob& epoch_pub_key,
                         const brillo::SecureBlob& epoch_priv_key,
                         const brillo::SecureBlob& ephemeral_pub_inv_key,
                         const HsmPayload& hsm_payload,
                         ResponsePayload* response_payload) const;

  // Decrypts `mediator_share` using `mediator_priv_key` from
  // `encrypted_mediator_share`. Returns false if error occurred.
  bool DecryptMediatorShare(
      const brillo::SecureBlob& mediator_priv_key,
      const RecoveryCrypto::EncryptedMediatorShare& encrypted_mediator_share,
      brillo::SecureBlob* mediator_share) const;

  // Decrypt `cipher_text` from `hsm_payload' using provided
  // `mediator_priv_key`.
  bool DecryptHsmPayloadPlainText(const brillo::SecureBlob& mediator_priv_key,
                                  const HsmPayload& hsm_payload,
                                  brillo::SecureBlob* plain_text) const;

  // Decrypt `cipher_text` from `request_payload' using provided
  // `epoch_priv_key` and store the result in `plain_text`.
  bool DecryptRequestPayloadPlainText(const brillo::SecureBlob& epoch_priv_key,
                                      const RequestPayload& request_payload,
                                      brillo::SecureBlob* plain_text) const;
  EllipticCurve ec_;
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_FAKE_RECOVERY_MEDIATOR_CRYPTO_H_
