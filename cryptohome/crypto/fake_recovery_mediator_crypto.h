// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_FAKE_RECOVERY_MEDIATOR_CRYPTO_H_
#define CRYPTOHOME_CRYPTO_FAKE_RECOVERY_MEDIATOR_CRYPTO_H_

#include <memory>

#include <brillo/secure_blob.h>

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/recovery_crypto.h"

namespace cryptohome {

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

 private:
  // Constructor is private. Use Create method to instantiate.
  explicit FakeRecoveryMediatorCrypto(EllipticCurve ec);

  // Decrypts `mediator_share` using `mediator_priv_key` from
  // `encrypted_mediator_share`. Returns false if error occurred.
  bool DecryptMediatorShare(
      const brillo::SecureBlob& mediator_priv_key,
      const RecoveryCrypto::EncryptedMediatorShare& encrypted_mediator_share,
      brillo::SecureBlob* mediator_share) const;

  EllipticCurve ec_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_FAKE_RECOVERY_MEDIATOR_CRYPTO_H_
