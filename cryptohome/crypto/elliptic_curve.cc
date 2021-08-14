// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/elliptic_curve.h"

#include <algorithm>
#include <utility>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <openssl/obj_mac.h>

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/error_util.h"

namespace cryptohome {

// static
base::Optional<EllipticCurve> EllipticCurve::Create(CurveType curve,
                                                    BN_CTX* context) {
  // Translate from CurveType to NID.
  int nid = NID_undef;
  switch (curve) {
    case CurveType::kPrime256:
      nid = NID_X9_62_prime256v1;
      break;
    case CurveType::kPrime384:
      nid = NID_secp384r1;
      break;
    case CurveType::kPrime521:
      nid = NID_secp521r1;
      break;
  }
  if (nid == NID_undef) {
    LOG(ERROR) << "Invalid curve type " << static_cast<int>(curve);
    return base::nullopt;
  }
  crypto::ScopedEC_GROUP group(EC_GROUP_new_by_curve_name(nid));
  if (!group) {
    LOG(ERROR) << "Failed to create group of EllipticCurve type="
               << static_cast<int>(curve) << " NID=" << nid << ": "
               << GetOpenSSLErrors();
    return base::nullopt;
  }
  crypto::ScopedBIGNUM order(BN_secure_new());
  if (!order) {
    LOG(ERROR) << "Failed to allocate BIGNUM structure: " << GetOpenSSLErrors();
    return base::nullopt;
  }
  if (!EC_GROUP_get_order(group.get(), order.get(), context)) {
    LOG(ERROR) << "Failed to get EC_GROUP order: " << GetOpenSSLErrors();
    return base::nullopt;
  }
  return EllipticCurve(std::move(group), std::move(order));
}

EllipticCurve::EllipticCurve(crypto::ScopedEC_GROUP group,
                             crypto::ScopedBIGNUM order)
    : group_(std::move(group)), order_(std::move(order)) {}

EllipticCurve::~EllipticCurve() = default;

crypto::ScopedEC_POINT EllipticCurve::PointAtInfinityForTesting() const {
  crypto::ScopedEC_POINT result(EC_POINT_new(group_.get()));
  if (!result) {
    LOG(ERROR) << "Failed to allocate EC_POINT structure: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  if (EC_POINT_set_to_infinity(group_.get(), result.get()) != 1) {
    LOG(ERROR) << "Failed to set point to infinity: " << GetOpenSSLErrors();
    return nullptr;
  }
  return result;
}

bool EllipticCurve::IsPointValid(const EC_POINT& point, BN_CTX* context) const {
  return EC_POINT_is_on_curve(group_.get(), &point, context) == 1;
}

bool EllipticCurve::IsPointAtInfinity(const EC_POINT& point) const {
  return EC_POINT_is_at_infinity(group_.get(), &point) == 1;
}

bool EllipticCurve::IsPointValidAndFinite(const EC_POINT& point,
                                          BN_CTX* context) const {
  return IsPointValid(point, context) && !IsPointAtInfinity(point);
}

bool EllipticCurve::InvertPoint(EC_POINT* point, BN_CTX* context) const {
  if (EC_POINT_invert(group_.get(), point, context) != 1) {
    LOG(ERROR) << "Failed to invert EC_POINT: " << GetOpenSSLErrors();
    return false;
  }
  return true;
}

int EllipticCurve::ScalarSizeInBytes() const {
  return BN_num_bytes(order_.get());
}

int EllipticCurve::FieldElementSizeInBytes() const {
  unsigned int degree_bits = EC_GROUP_get_degree(group_.get());
  return (degree_bits + 7) >> 3;
}

crypto::ScopedBIGNUM EllipticCurve::GetAffineCoordinateX(
    const EC_POINT& point, BN_CTX* context) const {
  if (!IsPointValidAndFinite(point, context)) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob: input point is "
                  "invalid or infinite";
    return nullptr;
  }
  crypto::ScopedBIGNUM x(BN_secure_new());
  if (EC_POINT_get_affine_coordinates(group_.get(), &point, x.get(),
                                      /*y=*/nullptr, context) != 1) {
    LOG(ERROR) << "Failed to get affine X coordinate: " << GetOpenSSLErrors();
    return nullptr;
  }
  return x;
}

crypto::ScopedBIGNUM EllipticCurve::RandomNonZeroScalar(BN_CTX* context) const {
  crypto::ScopedBIGNUM secret(BN_secure_new());
  if (!secret) {
    LOG(ERROR) << "Failed to allocate BIGNUM structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  // Loop until generated secret is non-zero.
  do {
    if (BN_priv_rand_range(secret.get(), order_.get()) != 1) {
      LOG(ERROR) << "Failed to generate secret: " << GetOpenSSLErrors();
      return nullptr;
    }
  } while (BN_is_zero(secret.get()));
  return secret;
}

crypto::ScopedBIGNUM EllipticCurve::ModAdd(const BIGNUM& a,
                                           const BIGNUM& b,
                                           BN_CTX* context) const {
  crypto::ScopedBIGNUM result(BN_secure_new());
  if (!result) {
    LOG(ERROR) << "Failed to allocate BIGNUM structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  if (BN_mod_add(result.get(), &a, &b, order_.get(), context) != 1) {
    LOG(ERROR) << "Failed to perform BIGNUM modulo addition: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  return result;
}

bool EllipticCurve::AreEqual(const EC_POINT& point1,
                             const EC_POINT& point2,
                             BN_CTX* context) const {
  int result = EC_POINT_cmp(group_.get(), &point1, &point2, context);
  if (result < 0) {
    LOG(ERROR) << "Failed to compare points: " << GetOpenSSLErrors();
    return false;
  }
  return result == 0;
}

crypto::ScopedEC_POINT EllipticCurve::Multiply(const EC_POINT& point,
                                               const BIGNUM& scalar,
                                               BN_CTX* context) const {
  if (!IsPointValid(point, context)) {
    LOG(ERROR)
        << "Failed to perform multiplication: input point is not on curve";
    return nullptr;
  }
  // Check if scalar is smaller than the curve order. Note that this is not
  // strictly required by EC_POINT_mul, but we make it here as a requirement for
  // safety and performance reasons - multiplication performs in constant time
  // if scalar is smaller than the curve order.
  if (BN_is_negative(&scalar) == 1 || BN_cmp(&scalar, order_.get()) >= 0) {
    LOG(ERROR) << "Failed to perform multiplication: input scalar is not "
                  "in the expected range [0..curve order-1]";
    return nullptr;
  }
  crypto::ScopedEC_POINT result(EC_POINT_new(group_.get()));
  if (!result) {
    LOG(ERROR) << "Failed to allocate EC_POINT structure: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  if (EC_POINT_mul(group_.get(), result.get(), nullptr, &point, &scalar,
                   context) != 1) {
    LOG(ERROR) << "Failed to perform multiplication: " << GetOpenSSLErrors();
    return nullptr;
  }
  return result;
}

crypto::ScopedEC_POINT EllipticCurve::MultiplyWithGenerator(
    const BIGNUM& scalar, BN_CTX* context) const {
  crypto::ScopedBIGNUM scalar_mod(BN_dup(&scalar));
  if (!scalar_mod) {
    LOG(ERROR) << "Failed to allocate BIGNUM structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  // We are normalizing scalar by adding the group order to it. If the result
  // is not in the range [0..curve order-1] fails as described in
  // EllipticCurve::Multiply.
  if (BN_is_negative(scalar_mod.get()) == 1) {
    if (!BN_add(scalar_mod.get(), order_.get(), scalar_mod.get())) {
      LOG(ERROR) << "Failed to add group order: " << GetOpenSSLErrors();
      return nullptr;
    }
  }
  if (BN_is_negative(scalar_mod.get()) == 1 ||
      BN_cmp(scalar_mod.get(), order_.get()) >= 0) {
    LOG(ERROR) << "Failed to perform multiplication: input scalar is not "
                  "in the expected range [-curve_order..curve order-1]";
    return nullptr;
  }
  crypto::ScopedEC_POINT result(EC_POINT_new(group_.get()));
  if (!result) {
    LOG(ERROR) << "Failed to allocate EC_POINT structure: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  if (EC_POINT_mul(group_.get(), result.get(), scalar_mod.get(), nullptr,
                   nullptr, context) != 1) {
    LOG(ERROR) << "Failed to perform multiplication with generator: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  return result;
}

crypto::ScopedEC_POINT EllipticCurve::Add(const EC_POINT& point1,
                                          const EC_POINT& point2,
                                          BN_CTX* context) const {
  if (!IsPointValid(point1, context) || !IsPointValid(point2, context)) {
    LOG(ERROR) << "Failed to perform addition: input point is not on curve";
    return nullptr;
  }
  crypto::ScopedEC_POINT result(EC_POINT_new(group_.get()));
  if (!result) {
    LOG(ERROR) << "Failed to allocate EC_POINT structure: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  if (EC_POINT_add(group_.get(), result.get(), &point1, &point2, context) !=
      1) {
    LOG(ERROR) << "Failed to perform addition: " << GetOpenSSLErrors();
    return nullptr;
  }
  return result;
}

crypto::ScopedEC_POINT EllipticCurve::SecureBlobToPoint(
    const brillo::SecureBlob& blob, BN_CTX* context) const {
  crypto::ScopedEC_POINT result(EC_POINT_new(group_.get()));
  if (!result) {
    LOG(ERROR) << "Failed to allocate EC_POINT structure: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  if (EC_POINT_oct2point(group_.get(), result.get(), blob.data(), blob.size(),
                         context) != 1) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  if (!IsPointValidAndFinite(*result, context)) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT: resulting point "
                  "is invalid or infinite";
    return nullptr;
  }
  return result;
}

size_t EllipticCurve::PointToBuf(const EC_POINT& point,
                                 crypto::ScopedOpenSSLBytes* ret_buf,
                                 BN_CTX* context) const {
  unsigned char* buf = nullptr;
  size_t buf_len = EC_POINT_point2buf(
      group_.get(), &point, POINT_CONVERSION_UNCOMPRESSED, &buf, context);
  ret_buf->reset(buf);
  return buf_len;
}

bool EllipticCurve::PointToSecureBlob(const EC_POINT& point,
                                      brillo::SecureBlob* result,
                                      BN_CTX* context) const {
  if (!IsPointValidAndFinite(point, context)) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob: input point is "
                  "invalid or infinite";
    return false;
  }
  crypto::ScopedOpenSSLBytes buf;
  size_t buf_len = PointToBuf(point, &buf, context);
  if (buf_len == 0) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob: "
               << GetOpenSSLErrors();
    return false;
  }
  result->assign(buf.get(), buf.get() + buf_len);
  return true;
}

crypto::ScopedEC_Key EllipticCurve::GenerateKey(BN_CTX* context) const {
  crypto::ScopedEC_Key key(EC_KEY_new());
  if (!key) {
    LOG(ERROR) << "Failed to allocate EC_KEY structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  if (EC_KEY_set_group(key.get(), group_.get()) != 1) {
    LOG(ERROR) << "Failed to set EC group: " << GetOpenSSLErrors();
    return nullptr;
  }
  if (EC_KEY_generate_key(key.get()) != 1) {
    LOG(ERROR) << "Failed to generate EC key: " << GetOpenSSLErrors();
    return nullptr;
  }
  return key;
}

bool EllipticCurve::GenerateKeysAsSecureBlobs(brillo::SecureBlob* public_key,
                                              brillo::SecureBlob* private_key,
                                              BN_CTX* context) const {
  crypto::ScopedEC_Key key = GenerateKey(context);
  if (!key) {
    LOG(ERROR) << "Failed to generate EC_KEY";
    return false;
  }
  if (!PointToSecureBlob(*EC_KEY_get0_public_key(key.get()), public_key,
                         context)) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob";
    return false;
  }
  if (!BigNumToSecureBlob(*EC_KEY_get0_private_key(key.get()),
                          ScalarSizeInBytes(), private_key)) {
    LOG(ERROR) << "Failed to convert BIGNUM to SecureBlob";
    return false;
  }
  return true;
}

}  // namespace cryptohome
