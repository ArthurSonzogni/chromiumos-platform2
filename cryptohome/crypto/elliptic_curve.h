// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_ELLIPTIC_CURVE_H_
#define CRYPTOHOME_CRYPTO_ELLIPTIC_CURVE_H_

#include <string>

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bn.h>
#include <openssl/ec.h>

namespace cryptohome {

// A light-weight C++ wrapper for OpenSSL Elliptic Curve primitives.
class EllipticCurve final {
 public:
  // Currently only most common prime curves are supported, but the interface
  // can be extended to any OpenSSL supported curve if needed.
  enum class CurveType { kPrime256, kPrime384, kPrime521 };

  // Creates an elliptic curve. Returns `base::nullopt` if error occurred.
  // Context is only used during creation of the curve and does not have to
  // outlive the curve instance (it is not stored in a curve object).
  static base::Optional<EllipticCurve> Create(CurveType curve, BN_CTX* context);

  // Non-copyable, but movable.
  EllipticCurve(EllipticCurve&& other) = default;
  EllipticCurve& operator=(EllipticCurve&& other) = default;

  ~EllipticCurve();

  // Returns true if point is on a curve (including point at infinity).
  bool IsPointValid(const EC_POINT& point, BN_CTX* context) const;

  // Returns true if point is at infinity.
  bool IsPointAtInfinity(const EC_POINT& point) const;

  // Returns true if point is on a curve and finite (not at infinity).
  bool IsPointValidAndFinite(const EC_POINT& point, BN_CTX* context) const;

  // Returns scalar size in bytes of a curve order.
  int ScalarSizeInBytes() const;

  // Returns field element (affine coordinate) size in bytes.
  int FieldElementSizeInBytes() const;

  // Returns affine X coordinate of a given `point` or nullptr if error
  // occurred.
  crypto::ScopedBIGNUM GetAffineCoordinateX(const EC_POINT& point,
                                            BN_CTX* context) const;

  // Generates random non-zero scalar of the elliptic curve order. Returns
  // nullptr if error occurred.
  crypto::ScopedBIGNUM RandomNonZeroScalar(BN_CTX* context) const;

  // Performs addition modulo order. Returns nullptr if error occurred.
  crypto::ScopedBIGNUM ModAdd(const BIGNUM& a,
                              const BIGNUM& b,
                              BN_CTX* context) const;

  // Returns true if two points are equal.
  bool AreEqual(const EC_POINT& point1,
                const EC_POINT& point2,
                BN_CTX* context) const;

  // Performs point by scalar multiplication. Input point must be on a curve.
  // It is required that scalar is in the range of [0..curve order-1].
  // Returns nullptr if error occurred.
  crypto::ScopedEC_POINT Multiply(const EC_POINT& point,
                                  const BIGNUM& scalar,
                                  BN_CTX* context) const;

  // Performs multiplication with generator. Expects scalar to be in the range
  // of [-curve order..curve order-1]. Return nullptr if error occurred.
  crypto::ScopedEC_POINT MultiplyWithGenerator(const BIGNUM& scalar,
                                               BN_CTX* context) const;

  // Performs point addition. Input points must be on a curve.
  // If two points are equal, the addition will perform doubling: P + P = 2P.
  // The result is a point on a curve or point at infinity e.g. P+(-P) = inf.
  // Returns nullptr if error occurred.
  crypto::ScopedEC_POINT Add(const EC_POINT& point1,
                             const EC_POINT& point2,
                             BN_CTX* context) const;

  // Converts SecureBlob to EC_POINT. Returns nullptr if error occurred.
  // The expected format of SecureBlob is OpenSSL octet form (a binary encoding
  // of the EC_POINT structure as defined in RFC5480).
  // The method returns an error if resulting point is not on a curve or at
  // infinity.
  crypto::ScopedEC_POINT SecureBlobToPoint(const brillo::SecureBlob& blob,
                                           BN_CTX* context) const;

  // Converts EC_POINT to SecureBlob. Input point must be finite and on a curve.
  // Returns false if error occurred, otherwise stores resulting blob in
  // `result`. The output blob is a point converted to OpenSSL uncompressed
  // octet form (a binary encoding of the EC_POINT structure as defined in
  // RFC5480).
  bool PointToSecureBlob(const EC_POINT& point,
                         brillo::SecureBlob* result,
                         BN_CTX* context) const;

  // Generates EC_KEY. This method should be preferred over generating private
  // and public key separately, that is, private key using `RandomNonZeroScalar`
  // and public key by multiplying private key with generator, but the result
  // should be equivalent. Returns nullptr if error occurred.
  crypto::ScopedEC_KEY GenerateKey(BN_CTX* context) const;

  // Generates pair EC_KEY and converts a pair of public and  them to secure
  // blobs, Returns false if error occurred.
  bool GenerateKeysAsSecureBlobs(brillo::SecureBlob* public_key,
                                 brillo::SecureBlob* private_key,
                                 BN_CTX* context) const;

  // Returns curve order. This should be used only for testing.
  const BIGNUM* GetOrderForTesting() const { return order_.get(); }

  // Returns point at infinity or nullptr if error occurred.
  // This should be used only for testing.
  crypto::ScopedEC_POINT PointAtInfinityForTesting() const;

  // Returns group. This should be used only for testing.
  const EC_GROUP* GetGroupForTesting() const { return group_.get(); }

 private:
  // Constructor is private. A user of the class should use `Create` method
  // instead.
  explicit EllipticCurve(crypto::ScopedEC_GROUP group,
                         crypto::ScopedBIGNUM order);

  // Converts point to buffer of bytes in OpenSSL octet form.
  // Returns length of buffer stored in `ret_buf` or zero if error occurred.
  size_t PointToBuf(const EC_POINT& point,
                    crypto::ScopedOpenSSLBytes* ret_buf,
                    BN_CTX* context) const;

  crypto::ScopedEC_GROUP group_;
  crypto::ScopedBIGNUM order_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_ELLIPTIC_CURVE_H_
