// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/crypto/secure_box.h"

#include <optional>
#include <utility>

#include <brillo/secure_blob.h>

#include "libhwsec-foundation/crypto/big_num_util.h"
#include "libhwsec-foundation/crypto/elliptic_curve.h"

namespace hwsec_foundation {
namespace secure_box {

namespace {
constexpr int kPublicKeyCoordinateSize = 256 / CHAR_BIT;
constexpr int kPrivateKeyScalarSize = 256 / CHAR_BIT;
constexpr uint8_t kEcPublicKeyUncompressedFormatPrefix = 4;
constexpr uint8_t kEcPublicKeyUncompressedFormatSize =
    1 + 2 * kPublicKeyCoordinateSize;
}  // namespace

std::optional<KeyPair> DeriveKeyPairFromSeed(const brillo::SecureBlob& seed) {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context) {
    LOG(ERROR) << "Failed to allocate BIGNUM context.";
    return std::nullopt;
  }
  std::optional<EllipticCurve> curve =
      EllipticCurve::Create(EllipticCurve::CurveType::kPrime256, context.get());
  if (!curve.has_value()) {
    LOG(ERROR) << "Failed to create EllipticCurve.";
  }

  crypto::ScopedBIGNUM seed_bn = SecureBlobToBigNum(seed);
  if (!seed_bn) {
    LOG(ERROR) << "Failed to transform seed to BIGNUM.";
    return std::nullopt;
  }
  crypto::ScopedBIGNUM priv_bn =
      curve->ModToValidNonZeroScalar(*seed_bn, context.get());
  if (!priv_bn) {
    LOG(ERROR) << "Failed to transform seed to a valid scalar on curve.";
    return std::nullopt;
  }
  brillo::SecureBlob private_key;
  if (!BigNumToSecureBlob(*priv_bn, kPrivateKeyScalarSize, &private_key)) {
    LOG(ERROR) << "Failed to transform private key scalar to SecureBlob.";
    return std::nullopt;
  }

  crypto::ScopedEC_POINT public_key_pt =
      curve->MultiplyWithGenerator(*priv_bn, context.get());
  if (!public_key_pt) {
    LOG(ERROR) << "Failed to calculate public key.";
    return std::nullopt;
  }
  crypto::ScopedBIGNUM pub_x_bn = CreateBigNum();
  crypto::ScopedBIGNUM pub_y_bn = CreateBigNum();
  if (!pub_x_bn || !pub_y_bn) {
    LOG(ERROR) << "Failed to allocate BIGNUM structures.";
    return std::nullopt;
  }
  if (!curve->GetAffineCoordinates(*public_key_pt, context.get(),
                                   pub_x_bn.get(), pub_y_bn.get())) {
    LOG(ERROR) << "Failed to get public key coordinates.";
    return std::nullopt;
  }
  brillo::Blob pub_x, pub_y;
  if (!BigNumToBlob(*pub_x_bn, kPublicKeyCoordinateSize, &pub_x) ||
      !BigNumToBlob(*pub_y_bn, kPublicKeyCoordinateSize, &pub_y)) {
    LOG(ERROR) << "Failed to transform public key coordinates to blobs.";
  }
  auto public_key = brillo::CombineBlobs(
      {brillo::Blob({kEcPublicKeyUncompressedFormatPrefix}), pub_x, pub_y});
  DCHECK_EQ(public_key.size(), kEcPublicKeyUncompressedFormatSize);
  // SecureBox's encoded private key format is the concatenation of the private
  // key and the public key. This is such that when the server side decrypts the
  // encrypted encoded private key, it contains the whole key pair.
  private_key = brillo::SecureBlob::Combine(
      private_key, brillo::SecureBlob(public_key.begin(), public_key.end()));

  return KeyPair{
      .public_key = std::move(public_key),
      .private_key = std::move(private_key),
  };
}

}  // namespace secure_box
}  // namespace hwsec_foundation
