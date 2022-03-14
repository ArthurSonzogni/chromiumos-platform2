// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/crypto/ecdh_hkdf.h"

#include <base/logging.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/sha.h>

#include "libhwsec-foundation/crypto/big_num_util.h"
#include "libhwsec-foundation/crypto/elliptic_curve.h"
#include "libhwsec-foundation/crypto/hkdf.h"

namespace hwsec_foundation {

bool ComputeEcdhSharedSecretPoint(
    const EllipticCurve& ec,
    const brillo::SecureBlob& others_pub_key,
    const brillo::SecureBlob& own_priv_key,
    brillo::SecureBlob* shared_secret_point_blob) {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  crypto::ScopedEC_POINT pub_point =
      ec.SecureBlobToPoint(others_pub_key, context.get());
  if (!pub_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT";
    return false;
  }
  crypto::ScopedBIGNUM priv_scalar = SecureBlobToBigNum(own_priv_key);
  if (!priv_scalar) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT shared_secret_point =
      ec.Multiply(*pub_point, *priv_scalar, context.get());
  if (!shared_secret_point) {
    LOG(ERROR) << "Failed to perform scalar multiplication";
    return false;
  }
  if (!ec.PointToSecureBlob(*shared_secret_point, shared_secret_point_blob,
                            context.get())) {
    LOG(ERROR) << "Failed to convert shared_secret_point to a SecureBlob";
    return false;
  }

  return true;
}

bool ComputeEcdhSharedSecret(const EllipticCurve& ec,
                             const brillo::SecureBlob& shared_secret_point_blob,
                             brillo::SecureBlob* shared_secret) {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  crypto::ScopedEC_POINT shared_secret_point =
      ec.SecureBlobToPoint(shared_secret_point_blob, context.get());
  if (!shared_secret_point) {
    LOG(ERROR)
        << "Failed to convert shared_secret_point SecureBlob to EC_POINT";
    return false;
  }
  // Get shared point's affine X coordinate.
  crypto::ScopedBIGNUM shared_x = CreateBigNum();
  if (!shared_x) {
    LOG(ERROR) << "Failed to allocate BIGNUM";
    return false;
  }
  if (!ec.GetAffineCoordinates(*shared_secret_point, context.get(),
                               shared_x.get(),
                               /*y=*/nullptr)) {
    LOG(ERROR) << "Failed to get shared_secret_point x coordinate";
    return false;
  }
  // Convert X coordinate to fixed-size blob.
  if (!BigNumToSecureBlob(*shared_x, ec.AffineCoordinateSizeInBytes(),
                          shared_secret)) {
    LOG(ERROR) << "Failed to convert BIGNUM to SecureBlob";
    return false;
  }
  return true;
}

bool ComputeHkdfWithInfoSuffix(const brillo::SecureBlob& hkdf_secret,
                               const brillo::SecureBlob& hkdf_info_suffix,
                               const brillo::SecureBlob& public_key,
                               const brillo::SecureBlob& hkdf_salt,
                               HkdfHash hkdf_hash,
                               size_t symmetric_key_len,
                               brillo::SecureBlob* symmetric_key) {
  // Compute HKDF using info = combined public_key and hkdf_info_suffix.
  brillo::SecureBlob info =
      brillo::SecureBlob::Combine(public_key, hkdf_info_suffix);
  return Hkdf(hkdf_hash, hkdf_secret, info, hkdf_salt, symmetric_key_len,
              symmetric_key);
}

bool GenerateEcdhHkdfSymmetricKey(
    const EllipticCurve& ec,
    const brillo::SecureBlob& shared_secret_point_blob,
    const brillo::SecureBlob& source_pub_key,
    const brillo::SecureBlob& hkdf_info_suffix,
    const brillo::SecureBlob& hkdf_salt,
    HkdfHash hkdf_hash,
    size_t symmetric_key_len,
    brillo::SecureBlob* symmetric_key) {
  brillo::SecureBlob shared_secret;
  if (!ComputeEcdhSharedSecret(ec, shared_secret_point_blob, &shared_secret)) {
    LOG(ERROR) << "Failed to compute shared secret";
    return false;
  }

  // Compute HKDF using info = combined source_pub_key and hkdf_info_suffix.
  brillo::SecureBlob info =
      brillo::SecureBlob::Combine(source_pub_key, hkdf_info_suffix);
  if (!ComputeHkdfWithInfoSuffix(shared_secret, hkdf_info_suffix,
                                 source_pub_key, hkdf_salt, hkdf_hash,
                                 symmetric_key_len, symmetric_key)) {
    LOG(ERROR) << "Failed to compute HKDF";
    return false;
  }
  // Dispose shared_secret after used
  shared_secret.clear();
  return true;
}

}  // namespace hwsec_foundation
