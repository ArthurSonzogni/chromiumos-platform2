// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_ECDH_HKDF_H_
#define CRYPTOHOME_CRYPTO_ECDH_HKDF_H_

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/hkdf.h"

#include <brillo/secure_blob.h>

namespace cryptohome {

// Computes ECDH shared secret. Returns nullptr if error occurred.
// The formula for shared secret:
//   shared_secret = GetAffineCoordinateX(pub_key * priv_key)
// This is intended to be equivalent to
// SubtleUtilBoringSSL::ComputeEcdhSharedSecret method implemented in Tink:
// https://github.com/google/tink/blob/1.5/cc/subtle/subtle_util_boringssl.cc
bool ComputeEcdhSharedSecret(const EllipticCurve& ec,
                             const brillo::SecureBlob& pub_key,
                             const brillo::SecureBlob& priv_key,
                             brillo::SecureBlob* shared_secret);

// Generates symmetric key of a given length for sender from recipient public
// key using ECDH+HKDF with `hkdf_salt` and `hkdf_info`. The resulting key is
// stored as `symmetric_key`. Returns false if operation failed. The formula
// used for generating keys:
//   shared_secret = (recipient_pub_key * ephemeral_priv_key).x
//   symmetric_key = HKDF((ephemeral_pub_key, shared_secret),
//     hkdf_info, hkdf_salt)
// where G is a EC group generator.
// This is intended to be equivalent to
// EciesHkdfNistPCurveSendKemBoringSsl::GenerateKey method implemented in Tink:
// https://github.com/google/tink/blob/1.5/cc/subtle/ecies_hkdf_sender_kem_boringssl.cc
bool GenerateEcdhHkdfSenderKey(const EllipticCurve& ec,
                               const brillo::SecureBlob& recipient_pub_key,
                               const brillo::SecureBlob& ephemeral_pub_key,
                               const brillo::SecureBlob& ephemeral_priv_key,
                               const brillo::SecureBlob& hkdf_info,
                               const brillo::SecureBlob& hkdf_salt,
                               HkdfHash hkdf_hash,
                               size_t symmetric_key_len,
                               brillo::SecureBlob* symmetric_key);

// Generates symmetric key of a given length for recipient from recipient
// private key and ephemeral public key using ECDH+HKDF with `hkdf_salt` and
// `hkdf_info`. The resulting key is stored in `symmetric_key`. Returns false if
// operation failed. The formula used for generating key:
//   shared_secret = (ephemeral_pub_key * recipient_priv_key).x
//   symmetric_key = HKDF((ephemeral_pub_key, shared_secret),
//     hkdf_info, hkdf_salt)
// Note that the resulting key should be the same as sender key, since
//   recipient_pub_key = G * recipient_priv_key,
//   ephemeral_pub_key = G * ephemeral_priv_key
//     => shared_secret1 = shared_secret2
//    <=> (recipient_priv_key * (G * ephemeral_priv_key)).x
//          = ((G * recipient_priv_key) * ephemeral_priv_key).x
// where G is a EC group generator.
// This is intended to be equivalent to
// EciesHkdfNistPCurveRecipientKemBoringSsl::GenerateKey method implemented in
// Tink:
// https://github.com/google/tink/blob/1.5/cc/subtle/ecies_hkdf_recipient_kem_boringssl.cc
bool GenerateEcdhHkdfRecipientKey(const EllipticCurve& ec,
                                  const brillo::SecureBlob& recipient_priv_key,
                                  const brillo::SecureBlob& ephemeral_pub_key,
                                  const brillo::SecureBlob& hkdf_info,
                                  const brillo::SecureBlob& hkdf_salt,
                                  HkdfHash hkdf_hash,
                                  size_t symmetric_key_len,
                                  brillo::SecureBlob* symmetric_key);

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_ECDH_HKDF_H_
