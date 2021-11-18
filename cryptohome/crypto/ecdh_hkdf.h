// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_ECDH_HKDF_H_
#define CRYPTOHOME_CRYPTO_ECDH_HKDF_H_

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/hkdf.h"

#include <brillo/secure_blob.h>

namespace cryptohome {

// Computes ECDH shared secret point. Returns nullptr if error occurred.
// The formula for shared secret point:
//   shared_secret_point = others_pub_key * own_priv_key
// Note that the resulting shared secret point should be the same as the
// other's, since
//   others_pub_key = G * others_priv_key,
//   own_pub_key = G * own_priv_key
//     => shared_secret_point1 = shared_secret_point2
//    <=> (others_priv_key * (G * own_priv_key)).x
//          = ((G * others_priv_key) * own_priv_key).x
// where G is a EC group generator.
bool ComputeEcdhSharedSecretPoint(const EllipticCurve& ec,
                                  const brillo::SecureBlob& others_pub_key,
                                  const brillo::SecureBlob& own_priv_key,
                                  brillo::SecureBlob* shared_secret_point_blob);

// Computes ECDH shared secret from the shared secret point. Returns nullptr if
// error occurred. The formula for shared secret:
//   shared_secret = x_coordinate of shared_secret_point =
//   (shared_secret_point).x
// This is intended to be equivalent to
// SubtleUtilBoringSSL::ComputeEcdhSharedSecret method implemented in Tink:
// https://github.com/google/tink/blob/1.5/cc/subtle/subtle_util_boringssl.cc
bool ComputeEcdhSharedSecret(const EllipticCurve& ec,
                             const brillo::SecureBlob& shared_secret_point_blob,
                             brillo::SecureBlob* shared_secret);

// Computes `symmetric_key` as:
//   symmetric_key = HKDF(hkdf_secret, (source_pub_key, hkdf_info_suffix),
//     hkdf_salt)
// The `source_pub_key` is concatenated with `hkdf_info_suffix` and is passed to
// HKDF as hkdf info field.
bool ComputeHkdfWithInfoSuffix(const brillo::SecureBlob& hkdf_secret,
                               const brillo::SecureBlob& hkdf_info_suffix,
                               const brillo::SecureBlob& source_pub_key,
                               const brillo::SecureBlob& hkdf_salt,
                               HkdfHash hkdf_hash,
                               size_t symmetric_key_len,
                               brillo::SecureBlob* symmetric_key);

// Generates symmetric key of a given length from a shared secret using
// ECDH+HKDF with `hkdf_salt` and `hkdf_info`. The resulting key is stored in
// `symmetric_key`. Returns false if operation failed. The formula used for
// generating key:
//   shared_secret = (shared_secret_point).x
//   symmetric_key = HKDF(shared_secret, (source_pub_key, hkdf_info_suffix),
//     hkdf_salt)
bool GenerateEcdhHkdfSymmetricKey(
    const EllipticCurve& ec,
    const brillo::SecureBlob& shared_secret_point_blob,
    const brillo::SecureBlob& source_pub_key,
    const brillo::SecureBlob& hkdf_info_suffix,
    const brillo::SecureBlob& hkdf_salt,
    HkdfHash hkdf_hash,
    size_t symmetric_key_len,
    brillo::SecureBlob* symmetric_key);

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_ECDH_HKDF_H_
