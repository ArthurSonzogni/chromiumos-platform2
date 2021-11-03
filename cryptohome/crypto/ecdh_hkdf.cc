// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/ecdh_hkdf.h"

#include <base/logging.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/sha.h>

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/hkdf.h"

namespace cryptohome {

bool ComputeEcdhSharedSecret(const EllipticCurve& ec,
                             const brillo::SecureBlob& pub_key,
                             const brillo::SecureBlob& priv_key,
                             brillo::SecureBlob* shared_secret) {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  crypto::ScopedEC_POINT pub_point =
      ec.SecureBlobToPoint(pub_key, context.get());
  if (!pub_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT";
    return false;
  }
  crypto::ScopedBIGNUM priv_scalar = SecureBlobToBigNum(priv_key);
  if (!priv_scalar) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT shared_point =
      ec.Multiply(*pub_point, *priv_scalar, context.get());
  if (!shared_point) {
    LOG(ERROR) << "Failed to perform scalar multiplication";
    return false;
  }
  // Get shared point's affine X coordinate.
  crypto::ScopedBIGNUM shared_x = CreateBigNum();
  if (!shared_x) {
    LOG(ERROR) << "Failed to allocate BIGNUM";
    return false;
  }
  if (!ec.GetAffineCoordinates(*shared_point, context.get(), shared_x.get(),
                               /*y=*/nullptr)) {
    LOG(ERROR) << "Failed to get shared_point x coordinate";
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

bool GenerateEcdhHkdfSenderKey(const EllipticCurve& ec,
                               const brillo::SecureBlob& recipient_pub_key,
                               const brillo::SecureBlob& ephemeral_pub_key,
                               const brillo::SecureBlob& ephemeral_priv_key,
                               const brillo::SecureBlob& hkdf_info_suffix,
                               const brillo::SecureBlob& hkdf_salt,
                               HkdfHash hkdf_hash,
                               size_t symmetric_key_len,
                               brillo::SecureBlob* symmetric_key) {
  brillo::SecureBlob shared_secret;
  if (!ComputeEcdhSharedSecret(ec, recipient_pub_key, ephemeral_priv_key,
                               &shared_secret)) {
    LOG(ERROR) << "Failed to compute shared secret";
    return false;
  }

  if (!ComputeHkdfWithInfoSuffix(shared_secret, hkdf_info_suffix,
                                 ephemeral_pub_key, hkdf_salt, hkdf_hash,
                                 symmetric_key_len, symmetric_key)) {
    LOG(ERROR) << "Failed to compute HKDF";
    return false;
  }
  return true;
}

bool GenerateEcdhHkdfRecipientKey(const EllipticCurve& ec,
                                  const brillo::SecureBlob& recipient_priv_key,
                                  const brillo::SecureBlob& ephemeral_pub_key,
                                  const brillo::SecureBlob& hkdf_info_suffix,
                                  const brillo::SecureBlob& hkdf_salt,
                                  HkdfHash hkdf_hash,
                                  size_t symmetric_key_len,
                                  brillo::SecureBlob* symmetric_key) {
  brillo::SecureBlob shared_secret;
  if (!ComputeEcdhSharedSecret(ec, ephemeral_pub_key, recipient_priv_key,
                               &shared_secret)) {
    LOG(ERROR) << "Failed to compute shared secret";
    return false;
  }

  // Compute HKDF using info = combined ephemeral_pub_key and hkdf_info_suffix.
  brillo::SecureBlob info =
      brillo::SecureBlob::Combine(ephemeral_pub_key, hkdf_info_suffix);
  if (!ComputeHkdfWithInfoSuffix(shared_secret, hkdf_info_suffix,
                                 ephemeral_pub_key, hkdf_salt, hkdf_hash,
                                 symmetric_key_len, symmetric_key)) {
    LOG(ERROR) << "Failed to compute HKDF";
    return false;
  }
  return true;
}

}  // namespace cryptohome
