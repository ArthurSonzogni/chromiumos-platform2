// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_H_
#define CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_H_

#include <memory>

#include <brillo/secure_blob.h>

#include "cryptohome/crypto/ecdh_hkdf.h"
#include "cryptohome/crypto/elliptic_curve.h"

namespace cryptohome {

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

  // Elliptic Curve type used by the protocol.
  static const EllipticCurve::CurveType kCurve;

  // Hash used by HKDF for encrypting mediator share.
  static const HkdfHash kHkdfHash;

  // Creates instance. Returns nullptr if error occurred.
  static std::unique_ptr<RecoveryCrypto> Create();

  virtual ~RecoveryCrypto();

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
  //   destination_recovery_key = HKDF((publisher_pub_key * destination_share
  //                                   + mediated_publisher_pub_key))
  virtual bool RecoverDestination(
      const brillo::SecureBlob& publisher_pub_key,
      const brillo::SecureBlob& destination_share,
      const brillo::SecureBlob& mediated_publisher_pub_key,
      brillo::SecureBlob* destination_recovery_key) const = 0;

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

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_H_
