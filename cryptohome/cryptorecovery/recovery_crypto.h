// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_H_

#include <memory>

#include <brillo/secure_blob.h>

#include "cryptohome/crypto/ecdh_hkdf.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {

// Cryptographic operations for cryptohome recovery.
// Recovery mechanism involves dealer, publisher, mediator and destination. The
// dealer is invoked during initial setup to generate random shares. The dealer
// functionality is implemented in `GenerateShares` method. The publisher
// performs the actual encryption of the cryptohome recovery key using a
// symmetric key derived from `publisher_dh` - the result of
// `GeneratePublisherKeys` method. The mediator is an external service that is
// invoked during the recovery process to perform mediation of an encrypted
// mediator share. The destination is invoked as part of the recovery UX on the
// device to obtain a cryptohome recovery key. The recovery key can be derived
// from `destination_dh` - the result of `RecoverDestination` method. Note that
// in a successful recovery `destination_dh` should be equal to `publisher_dh`.
class RecoveryCrypto {
 public:
  // Mediator share is encrypted using AES-GCM with symmetric key derived from
  // ECDH+HKDF over mediator public key and ephemeral public key.
  // Ephemeral public key `ephemeral_pub_key`, AES-GCM `tag` and `iv` are stored
  // in the structure as they are necessary to perform decryption.
  struct EncryptedMediatorShare {
    brillo::SecureBlob tag;
    brillo::SecureBlob iv;
    brillo::SecureBlob ephemeral_pub_key;
    brillo::SecureBlob encrypted_data;
  };

  // Constant value of hkdf_info for mediator share. Must be kept in sync with
  // the server.
  static const char kMediatorShareHkdfInfoValue[];

  // Constant value of hkdf_info for request payload plaintext. Must be kept in
  // sync with the server.
  static const char kRequestPayloadPlainTextHkdfInfoValue[];

  // Constant value of hkdf_info for response payload plaintext. Must be kept in
  // sync with the server.
  static const char kResponsePayloadPlainTextHkdfInfoValue[];

  // Elliptic Curve type used by the protocol.
  static const EllipticCurve::CurveType kCurve;

  // Hash used by HKDF for encrypting mediator share.
  static const HkdfHash kHkdfHash;

  // Length of the salt (in bytes) used by HKDF for encrypting mediator share.
  static const unsigned int kHkdfSaltLength;

  // Creates instance. Returns nullptr if error occurred.
  static std::unique_ptr<RecoveryCrypto> Create();

  virtual ~RecoveryCrypto();

  // Generates Request payload that will be sent to Recovery Mediator Service
  // during recovery process.
  // Consist of the following steps:
  // 1. Construct associated data AD2 = {hsm_payload, `request_metadata`}.
  // 2. Generate symmetric key for encrypting plain text from (G*r)*s
  // (`epoch_pub_key` * `channel_priv_key`).
  // 3. Generate ephemeral key pair {x, G*x} and calculate an inverse G*-x.
  // 4. Save G*x to `ephemeral_pub_key` parameter.
  // 5. Construct plain text PT2 = {G*-x}.
  // 6. Encrypt {AD2, PT2} using AES-GCM scheme.
  // 7. Construct `RecoveryRequest` and serialize it to
  // `recovery_request` CBOR.
  virtual bool GenerateRecoveryRequest(
      const HsmPayload& hsm_payload,
      const brillo::SecureBlob& request_meta_data,
      const brillo::SecureBlob& channel_priv_key,
      const brillo::SecureBlob& channel_pub_key,
      const brillo::SecureBlob& epoch_pub_key,
      brillo::SecureBlob* recovery_request,
      brillo::SecureBlob* ephemeral_pub_key) const = 0;

  // Generates HSM payload that will be persisted on a chromebook at enrollment
  // to be subsequently used for recovery.
  // Consist of the following steps:
  // 1. Generate publisher key pair (u, G * u according to the protocol spec).
  // 2. Generate dealer key pair (a, G * a)
  // 3. Generate 2 shares: mediator (b1) and destination (b2).
  // 4. Generate channel key pair (s, G*s) and set `channel_priv_key` and
  // `channel_pub_key`.
  // 5. Construct associated data {G*s, G*u, `rsa_pub_key`,
  // `onboarding_metadata`}.
  // 6. Construct plain text {G*a, b2, kav} (note kav == key auth value is used
  // only in TPM 1.2 and will be generated for non-empty `rsa_pub_key`).
  // 7. Calculate shared secret G*(a(b1+b2)) and convert it to the
  // `recovery_key`.
  // 8. Generate symmetric key for encrypting PT from (G*h)*u (where G*h is the
  // mediator public key provided as input).
  // 9. Encrypt {AD, PT} using AES-GCM scheme.
  //
  // G*s is included in associated data, s is either wrapped with TPM 2.0 or
  // stored in host for TPM 1.2.
  // The resulting destination share should be either added to TPM 2.0 or sealed
  // with kav for TPM 1.2 and stored in the host.
  virtual bool GenerateHsmPayload(
      const brillo::SecureBlob& mediator_pub_key,
      const brillo::SecureBlob& rsa_pub_key,
      const brillo::SecureBlob& onboarding_metadata,
      HsmPayload* hsm_payload,
      brillo::SecureBlob* destination_share,
      brillo::SecureBlob* recovery_key,
      brillo::SecureBlob* channel_pub_key,
      brillo::SecureBlob* channel_priv_key) const = 0;

  // Generates shares for recovery. Returns false if error occurred.
  // Formula:
  //   dealer_pub_key = G * (mediator_share + destination_share (mod order))
  // where G is an elliptic curve group generator.
  // Encrypts `mediator_share` to `mediator_pub_key` and returns as
  // `encrypted_mediator_share`.
  virtual bool GenerateShares(const brillo::SecureBlob& mediator_pub_key,
                              EncryptedMediatorShare* encrypted_mediator_share,
                              brillo::SecureBlob* destination_share,
                              brillo::SecureBlob* dealer_pub_key) const = 0;

  // Generates publisher public keys. Returns false if error occurred.
  // Formula:
  //   publisher_pub_key = G * secret
  //   publisher_recovery_key = HKDF((dealer_pub_key * secret))
  // where G is an elliptic curve group generator.
  virtual bool GeneratePublisherKeys(
      const brillo::SecureBlob& dealer_pub_key,
      brillo::SecureBlob* publisher_pub_key,
      brillo::SecureBlob* publisher_recovery_key) const = 0;

  // Recovers destination. Returns false if error occurred.
  // Formula:
  //   mediated_point = `mediated_publisher_pub_key` + `ephemeral_pub_key`
  //   destination_recovery_key = HKDF((publisher_pub_key * destination_share
  //                                   + mediated_point))
  virtual bool RecoverDestination(
      const brillo::SecureBlob& publisher_pub_key,
      const brillo::SecureBlob& destination_share,
      const base::Optional<brillo::SecureBlob>& ephemeral_pub_key,
      const brillo::SecureBlob& mediated_publisher_pub_key,
      brillo::SecureBlob* destination_recovery_key) const = 0;

  // Decrypt plain text from the Recovery Response.
  // Consists of the following steps:
  // 1. Deserialize `recovery_response_cbor` to `RecoveryResponse`.
  // 2. Get cipher text, associated data, AES-GCM tag and iv from
  // `response_payload` field of `RecoveryResponse`
  // 3. Decrypt cipher text of response payload and store the
  // result in `response_plain_text`. The key for decryption is
  // HKDF(ECDH(channel_priv_key, epoch_pub_key)).
  virtual bool DecryptResponsePayload(
      const brillo::SecureBlob& channel_priv_key,
      const brillo::SecureBlob& epoch_pub_key,
      const brillo::SecureBlob& recovery_response_cbor,
      brillo::SecureBlob* response_plain_text) const = 0;

  // Serialize `encrypted_mediator_share` by simply concatenating fixed-length
  // blobs into `serialized_blob`. Returns false if error occurred.
  static bool SerializeEncryptedMediatorShareForTesting(
      const EncryptedMediatorShare& encrypted_mediator_share,
      brillo::SecureBlob* serialized_blob);

  // Deserialize `encrypted_mediator_share` assuming `serialized_blob` contains
  // chunks representing encrypted mediator share fixed-length blobs. Returns
  // false if error occurred.
  static bool DeserializeEncryptedMediatorShareForTesting(
      const brillo::SecureBlob& serialized_blob,
      EncryptedMediatorShare* encrypted_mediator_share);
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_H_
