// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_H_
#define CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_H_

#include <memory>

#include <brillo/secure_blob.h>

namespace cryptohome {

// Cryptographic operations for cryptohome recovery.
// Recovery mechanism involves dealer, publisher, mediator and destination. The
// dealer is invoked during initial setup to generate random shares. The dealer
// functionality is implemented in `GenerateShares` method. The publisher
// performs the actual encryption of the cryptohome recovery key using a
// symmetric key derived from `publisher_dh` - the result of
// `GeneratePublisherKeys` method. The mediator is an external service that is
// invoked during the recovery process to perform mediation of an encrypted
// mediator share. The functionality of mediator should be implemented on the
// server and here it is implemented for testing purposes only. The destination
// is invoked as part of the recovery UX on the device to obtain a cryptohome
// recovery key. The recovery key can be derived from `destination_dh` - the
// result of `RecoverDestination` method. Note that in a successful recovery
// `destination_dh` should be equal to `publisher_dh`.
class RecoveryCrypto {
 public:
  // Creates instance. Returns nullptr if error occurred.
  static std::unique_ptr<RecoveryCrypto> Create();

  virtual ~RecoveryCrypto();

  // Generates shares for recovery. Returns false if error occurred.
  // Formula:
  //   dealer_pub_key = G * (mediator_share + destination_share (mod order))
  // where G is an elliptic curve group generator.
  // TODO(b:180716332): return encrypted `mediator_share`.
  virtual bool GenerateShares(brillo::SecureBlob* mediator_share,
                              brillo::SecureBlob* destination_share,
                              brillo::SecureBlob* dealer_pub_key) const = 0;

  // Generates publisher public keys. Returns false if error occurred.
  // Formula:
  //   publisher_pub_key = G * secret
  //   publisher_dh = dealer_pub_key * secret
  // where G is an elliptic curve group generator.
  virtual bool GeneratePublisherKeys(
      const brillo::SecureBlob& dealer_pub_key,
      brillo::SecureBlob* publisher_pub_key,
      brillo::SecureBlob* publisher_dh) const = 0;

  // Performs mediation. Returns false if error occurred.
  // Formula:
  //   mediated_publisher_pub_key = publisher_pub_key * mediator_share
  // TODO(b:180716332): pass encrypted `mediator_share`.
  virtual bool Mediate(
      const brillo::SecureBlob& publisher_pub_key,
      const brillo::SecureBlob& mediator_share,
      brillo::SecureBlob* mediated_publisher_pub_key) const = 0;

  // Recovers destination. Returns false if error occurred.
  // Formula:
  //   destination_dh = publisher_pub_key * destination_share
  //     + mediated_publisher_pub_key
  virtual bool RecoverDestination(
      const brillo::SecureBlob& publisher_pub_key,
      const brillo::SecureBlob& destination_share,
      const brillo::SecureBlob& mediated_publisher_pub_key,
      brillo::SecureBlob* destination_dh) const = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_H_
