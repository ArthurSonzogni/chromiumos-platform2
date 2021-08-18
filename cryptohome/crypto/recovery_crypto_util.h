// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_UTIL_H_
#define CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_UTIL_H_

#include <brillo/secure_blob.h>

namespace cryptohome {
namespace cryptorecovery {

// AEAD-encrypted payload.
struct AeadPayload {
  // AES-GCM tag for encryption.
  brillo::SecureBlob tag;
  // AES-GCM iv for encryption.
  brillo::SecureBlob iv;
  // Additional authentication data, passed in clear. Serialized in cbor.
  brillo::SecureBlob associated_data;
  // Encrypted plain text. Plain text is serialized in cbor.
  brillo::SecureBlob cipher_text;
};

// HSM Payload is created at onboarding and contains all the data that are
// persisted on a chromebook and will be eventually used for recovery.
using HsmPayload = AeadPayload;

// Recovery Request Payload is created during recovery flow.
// `associated_data` contains data from `HsmPayload`, request metadata (RMD),
// and epoch public key (G*r).
using RequestPayload = AeadPayload;

// HSM response. Contains response associated data AD3 = {kav, HMD}
// (where kav is Key Auth Value and HMD is HSM Metadata) and plain text
// response PT3 = {dealer_pub_key, mediated_share} encrypted with
// DH of epoch and channel_pub_key.
using ResponsePayload = AeadPayload;

// `associated_data` for the HSM payload.
// `publisher_pub_key` and `channel_pub_key` are elliptic curve points
// encoded in OpenSSL octet form (a binary encoding of the EC_POINT
// structure as defined in RFC5480).
// TODO(mslus): exact format of rsa_public_key used for TPM 1.2 is
// to be defined.
struct HsmAssociatedData {
  // G*u, one of the keys that will be used for HSM payload decryption.
  brillo::SecureBlob publisher_pub_key;
  // G*s, one of the keys that will be used for Request payload decryption.
  brillo::SecureBlob channel_pub_key;
  // The key sent to HSM so that it can validate Request payload, used only for
  // TPM 1.2.
  brillo::SecureBlob rsa_public_key;
  // The metadata generated during the Onboarding workflow on a Chromebook
  // (OMD).
  brillo::SecureBlob onboarding_meta_data;
};

// Plain text for the HSM payload.
// `dealer_pub_key` is an elliptic curve point encoded in OpenSSL octet form (a
// binary encoding of the EC_POINT structure as defined in RFC5480).
// `mediator_share` and `key_auth_value` are BIGNUMs encoded in big-endian
// form.
struct HsmPlainText {
  // Secret share of the Mediator (b1).
  brillo::SecureBlob mediator_share;
  // Key generated on Chromebook, to be sent to the Mediator service (G*a).
  brillo::SecureBlob dealer_pub_key;
  // Additional secret to seal the destination share. Used for TPM 1.2 only.
  brillo::SecureBlob key_auth_value;
};

// `associated_data` for the Request payload.
struct RecoveryRequestAssociatedData {
  // HSM payload ciphertext (CT1).
  brillo::SecureBlob hsm_aead_ct;
  // HSM payload associated data (AD1).
  brillo::SecureBlob hsm_aead_ad;
  // AES-GCM iv for AEAD of the HSM payload.
  brillo::SecureBlob hsm_aead_iv;
  // AES-GCM tag for AEAD of the HSM payload.
  brillo::SecureBlob hsm_aead_tag;
  // The metadata generated during the Recovery flow on a Chromebook (RMD).
  brillo::SecureBlob request_meta_data;
  // Current epoch beacon value (G*r).
  brillo::SecureBlob epoch_pub_key;
};

// Plain text for the Request payload.
// `ephemeral_pub_inv_key` is an elliptic curve point encoded in OpenSSL octet
// form (a binary encoding of the EC_POINT structure as defined in RFC5480).
struct RecoveryRequestPlainText {
  // Ephemeral inverse key (G*-x) that is added to mediator DH (G*ab1) by the
  // Mediator service.
  brillo::SecureBlob ephemeral_pub_inv_key;
};

// Plain text for the Response payload.
// `dealer_pub_key` and `mediated_point` are elliptic curve points encoded in
// OpenSSL octet form (a binary encoding of the EC_POINT structure as defined in
// RFC5480). `key_auth_value` is BIGNUM encoded in big-endian form.
struct HsmResponsePlainText {
  // Mediated mediator share (b1) sent back to the Chromebook.
  brillo::SecureBlob mediated_point;
  // Key generated on Chromebook, that was used for mediation (G*a).
  brillo::SecureBlob dealer_pub_key;
  // Additional secret to seal the destination share. Used for TPM 1.2 only.
  brillo::SecureBlob key_auth_value;
};

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_UTIL_H_
