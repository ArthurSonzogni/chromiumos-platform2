// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/recovery_crypto.h"

#include <utility>

#include <base/logging.h>
#include <base/stl_util.h>

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/error_util.h"

namespace cryptohome {

namespace {

constexpr EllipticCurve::CurveType kCurve = EllipticCurve::CurveType::kPrime256;

// Cryptographic operations for cryptohome recovery performed on CPU (software
// emulation).
class RecoveryCryptoImpl : public RecoveryCrypto {
 public:
  explicit RecoveryCryptoImpl(EllipticCurve ec);

  ~RecoveryCryptoImpl() override;

  bool GenerateShares(brillo::SecureBlob* mediator_share,
                      brillo::SecureBlob* destination_share,
                      brillo::SecureBlob* dealer_pub_key) const override;
  bool GeneratePublisherKeys(const brillo::SecureBlob& dealer_pub_key,
                             brillo::SecureBlob* publisher_pub_key,
                             brillo::SecureBlob* publisher_dh) const override;
  bool Mediate(const brillo::SecureBlob& publisher_pub_key,
               const brillo::SecureBlob& mediator_share,
               brillo::SecureBlob* mediated_publisher_pub_key) const override;
  bool RecoverDestination(const brillo::SecureBlob& publisher_pub_key,
                          const brillo::SecureBlob& destination_share,
                          const brillo::SecureBlob& mediated_publisher_pub_key,
                          brillo::SecureBlob* destination_dh) const override;

 private:
  EllipticCurve ec_;
};

RecoveryCryptoImpl::RecoveryCryptoImpl(EllipticCurve ec) : ec_(std::move(ec)) {}

RecoveryCryptoImpl::~RecoveryCryptoImpl() = default;

bool RecoveryCryptoImpl::GenerateShares(
    brillo::SecureBlob* mediator_share,
    brillo::SecureBlob* destination_share,
    brillo::SecureBlob* dealer_pub_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  // Generate two shares and a secret equal to the sum.
  // Loop until the sum of two shares is non-zero (modulo order).
  crypto::ScopedBIGNUM secret;
  crypto::ScopedBIGNUM destination_share_bn =
      ec_.RandomNonZeroScalar(context.get());
  if (!destination_share_bn) {
    LOG(ERROR) << "Failed to generate secret";
    return false;
  }
  crypto::ScopedBIGNUM mediator_share_bn;
  do {
    mediator_share_bn = ec_.RandomNonZeroScalar(context.get());
    if (!mediator_share_bn) {
      LOG(ERROR) << "Failed to generate secret";
      return false;
    }
    secret =
        ec_.ModAdd(*mediator_share_bn, *destination_share_bn, context.get());
    if (!secret) {
      LOG(ERROR) << "Failed to perform modulo addition";
      return false;
    }
  } while (BN_is_zero(secret.get()));

  crypto::ScopedEC_POINT dealer_pub_point =
      ec_.MultiplyWithGenerator(*secret, context.get());
  if (!dealer_pub_point) {
    LOG(ERROR) << "Failed to perform MultiplyWithGenerator operation";
    return false;
  }
  if (!BigNumToSecureBlob(*mediator_share_bn, ec_.ScalarSizeInBytes(),
                          mediator_share)) {
    LOG(ERROR) << "Failed to convert BIGNUM to SecureBlob";
    return false;
  }
  if (!BigNumToSecureBlob(*destination_share_bn, ec_.ScalarSizeInBytes(),
                          destination_share)) {
    LOG(ERROR) << "Failed to convert BIGNUM to SecureBlob";
    return false;
  }
  if (!ec_.PointToSecureBlob(*dealer_pub_point, dealer_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob";
    return false;
  }
  return true;
}

bool RecoveryCryptoImpl::GeneratePublisherKeys(
    const brillo::SecureBlob& dealer_pub_key,
    brillo::SecureBlob* publisher_pub_key,
    brillo::SecureBlob* publisher_dh) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  crypto::ScopedBIGNUM secret = ec_.RandomNonZeroScalar(context.get());
  if (!secret) {
    LOG(ERROR) << "Failed to generate secret";
    return false;
  }
  crypto::ScopedEC_POINT publisher_pub_point =
      ec_.MultiplyWithGenerator(*secret, context.get());
  if (!publisher_pub_point) {
    LOG(ERROR) << "Failed to perform MultiplyWithGenerator operation";
    return false;
  }
  crypto::ScopedEC_POINT dealer_pub_point =
      ec_.SecureBlobToPoint(dealer_pub_key, context.get());
  if (!dealer_pub_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_point";
    return false;
  }
  crypto::ScopedEC_POINT point_dh =
      ec_.Multiply(*dealer_pub_point, *secret, context.get());
  if (!point_dh) {
    LOG(ERROR) << "Failed to perform point multiplication";
    return false;
  }
  if (!ec_.PointToSecureBlob(*publisher_pub_point, publisher_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob";
    return false;
  }
  if (!ec_.PointToSecureBlob(*point_dh, publisher_dh, context.get())) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob";
    return false;
  }
  return true;
}

bool RecoveryCryptoImpl::Mediate(
    const brillo::SecureBlob& publisher_pub_key,
    const brillo::SecureBlob& mediator_share,
    brillo::SecureBlob* mediated_publisher_pub_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  crypto::ScopedBIGNUM mediator_share_bn = SecureBlobToBigNum(mediator_share);
  if (!mediator_share_bn) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT publisher_pub_point =
      ec_.SecureBlobToPoint(publisher_pub_key, context.get());
  if (!publisher_pub_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT";
    return false;
  }
  // Performs scalar multiplication of publisher_pub_key and mediator_share.
  crypto::ScopedEC_POINT point_dh =
      ec_.Multiply(*publisher_pub_point, *mediator_share_bn, context.get());
  if (!point_dh) {
    LOG(ERROR) << "Failed to perform scalar multiplication";
    return false;
  }
  if (!ec_.PointToSecureBlob(*point_dh, mediated_publisher_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob";
    return false;
  }
  return true;
}

bool RecoveryCryptoImpl::RecoverDestination(
    const brillo::SecureBlob& publisher_pub_key,
    const brillo::SecureBlob& destination_share,
    const brillo::SecureBlob& mediated_publisher_pub_key,
    brillo::SecureBlob* destination_dh) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  crypto::ScopedBIGNUM destination_share_bn =
      SecureBlobToBigNum(destination_share);
  if (!destination_share_bn) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT publisher_pub_point =
      ec_.SecureBlobToPoint(publisher_pub_key, context.get());
  if (!publisher_pub_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT";
    return false;
  }
  crypto::ScopedEC_POINT mediated_publisher_pub_point =
      ec_.SecureBlobToPoint(mediated_publisher_pub_key, context.get());
  if (!mediated_publisher_pub_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT";
    return false;
  }
  // Performs scalar multiplication of publisher_pub_key and destination_share.
  crypto::ScopedEC_POINT point_dh =
      ec_.Multiply(*publisher_pub_point, *destination_share_bn, context.get());
  if (!point_dh) {
    LOG(ERROR) << "Failed to perform scalar multiplication";
    return false;
  }
  crypto::ScopedEC_POINT point_dest =
      ec_.Add(*point_dh, *mediated_publisher_pub_point, context.get());
  if (!point_dest) {
    LOG(ERROR) << "Failed to perform point addition";
    return false;
  }
  if (!ec_.PointToSecureBlob(*point_dest, destination_dh, context.get())) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob";
    return false;
  }
  return true;
}

}  // namespace

std::unique_ptr<RecoveryCrypto> RecoveryCrypto::Create() {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return nullptr;
  }
  base::Optional<EllipticCurve> ec =
      EllipticCurve::Create(kCurve, context.get());
  if (!ec) {
    LOG(ERROR) << "Failed to create EllipticCurve";
    return nullptr;
  }
  return std::make_unique<RecoveryCryptoImpl>(std::move(*ec));
}

RecoveryCrypto::~RecoveryCrypto() = default;

}  // namespace cryptohome
