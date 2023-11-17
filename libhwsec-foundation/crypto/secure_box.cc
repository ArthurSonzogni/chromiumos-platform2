// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/crypto/secure_box.h"

#include <iterator>
#include <optional>
#include <utility>

#include <brillo/secure_blob.h>

#include "libhwsec-foundation/crypto/aes.h"
#include "libhwsec-foundation/crypto/big_num_util.h"
#include "libhwsec-foundation/crypto/ecdh_hkdf.h"
#include "libhwsec-foundation/crypto/elliptic_curve.h"
#include "libhwsec-foundation/crypto/hkdf.h"

namespace hwsec_foundation {
namespace secure_box {

namespace {
constexpr size_t kAES128KeySize = 128 / CHAR_BIT;
constexpr int kPublicKeyCoordinateSize = 256 / CHAR_BIT;
constexpr int kPrivateKeyScalarSize = 256 / CHAR_BIT;
constexpr uint8_t kEcPublicKeyUncompressedFormatPrefix = 4;
constexpr uint8_t kEcPublicKeyUncompressedFormatSize =
    1 + 2 * kPublicKeyCoordinateSize;
constexpr uint8_t kSecureBoxVersion[] = {0x02, 0};
constexpr char kHkdfSaltPrefix[] = "SECUREBOX";
constexpr char kHkdfInfoWithPublicKey[] = "P256 HKDF-SHA-256 AES-128-GCM";
constexpr char kHkdfInfoWithoutPublicKey[] = "SHARED HKDF-SHA-256 AES-128-GCM";

crypto::ScopedEC_POINT DecodePublicKey(const EllipticCurve& curve,
                                       BN_CTX* context,
                                       const brillo::Blob& public_key) {
  if (public_key.size() != kEcPublicKeyUncompressedFormatSize) {
    LOG(ERROR) << "Incorrect public key size.";
    return nullptr;
  }
  if (public_key[0] != kEcPublicKeyUncompressedFormatPrefix) {
    LOG(ERROR) << "Incorrect public key prefix.";
    return nullptr;
  }
  crypto::ScopedBIGNUM pub_x_bn = BlobToBigNum(
      brillo::Blob(public_key.begin() + 1,
                   public_key.begin() + 1 + kPublicKeyCoordinateSize));
  crypto::ScopedBIGNUM pub_y_bn = BlobToBigNum(brillo::Blob(
      public_key.begin() + 1 + kPublicKeyCoordinateSize, public_key.end()));
  if (!pub_x_bn || !pub_y_bn) {
    LOG(ERROR) << "Failed to transform public key coordinates to BIGNUM.";
    return nullptr;
  }
  crypto::ScopedEC_POINT point = curve.CreatePoint();
  if (!point) {
    LOG(ERROR) << "Failed to allocate EC point.";
    return nullptr;
  }
  if (!EC_POINT_set_affine_coordinates(curve.GetGroup(), point.get(),
                                       pub_x_bn.get(), pub_y_bn.get(),
                                       context)) {
    LOG(ERROR) << "Failed to set affine coordinates.";
    return nullptr;
  }
  if (!curve.IsPointValidAndFinite(*point, context)) {
    LOG(ERROR) << "Decoded point is invalid.";
    return nullptr;
  }
  return point;
}

std::optional<brillo::Blob> EncodePublicKey(const EllipticCurve& curve,
                                            BN_CTX* context,
                                            const EC_POINT& public_key_pt) {
  crypto::ScopedBIGNUM pub_x_bn = CreateBigNum();
  crypto::ScopedBIGNUM pub_y_bn = CreateBigNum();
  if (!pub_x_bn || !pub_y_bn) {
    LOG(ERROR) << "Failed to allocate BIGNUM structures.";
    return std::nullopt;
  }
  if (!curve.GetAffineCoordinates(public_key_pt, context, pub_x_bn.get(),
                                  pub_y_bn.get())) {
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
  return public_key;
}

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

  std::optional<brillo::Blob> public_key =
      EncodePublicKey(*curve, context.get(), *public_key_pt);
  if (!public_key.has_value()) {
    LOG(ERROR) << "Failed to encode public key.";
    return std::nullopt;
  }
  // SecureBox's encoded private key format is the concatenation of the private
  // key and the public key. This is such that when the server side decrypts the
  // encrypted encoded private key, it contains the whole key pair.
  private_key = brillo::SecureBlob::Combine(
      private_key, brillo::SecureBlob(public_key->begin(), public_key->end()));

  return KeyPair{
      .public_key = std::move(*public_key),
      .private_key = std::move(private_key),
  };
}

std::optional<brillo::Blob> Encrypt(const brillo::Blob& their_public_key,
                                    const brillo::SecureBlob& shared_secret,
                                    const brillo::Blob& header,
                                    const brillo::SecureBlob& payload) {
  if (their_public_key.empty() && shared_secret.empty()) {
    LOG(ERROR) << "Either public key or shared secret should be non-empty.";
    return std::nullopt;
  }

  // If |their_public_key| is empty, asymmetric encryption isn't used, so
  // |our_key_pair| and |our_public_key| will both be null. In this case,
  // |our_public_key| won't be included in the concatenated encryption result
  // buffer.
  crypto::ScopedEC_KEY our_key_pair;
  brillo::SecureBlob dh_secret;
  brillo::Blob hkdf_info, our_public_key;
  if (their_public_key.empty()) {
    hkdf_info = brillo::BlobFromString(kHkdfInfoWithoutPublicKey);
  } else {
    ScopedBN_CTX context = CreateBigNumContext();
    if (!context) {
      LOG(ERROR) << "Failed to allocate BIGNUM context.";
      return std::nullopt;
    }
    std::optional<EllipticCurve> curve = EllipticCurve::Create(
        EllipticCurve::CurveType::kPrime256, context.get());
    if (!curve.has_value()) {
      LOG(ERROR) << "Failed to create EllipticCurve.";
      return std::nullopt;
    }
    // Parse their public key.
    crypto::ScopedEC_POINT their_public_key_pt =
        DecodePublicKey(*curve, context.get(), their_public_key);
    if (!their_public_key_pt) {
      LOG(ERROR) << "Failed to decode their public key.";
      return std::nullopt;
    }
    // Generate our key pair.
    our_key_pair = curve->GenerateKey(context.get());
    if (!our_key_pair) {
      LOG(ERROR) << "Failed to generate EC key.";
      return std::nullopt;
    }
    const BIGNUM* private_key_bn = EC_KEY_get0_private_key(our_key_pair.get());
    const EC_POINT* public_key_pt = EC_KEY_get0_public_key(our_key_pair.get());
    if (!private_key_bn || !public_key_pt) {
      LOG(ERROR) << "Failed to generate EC key.";
      return std::nullopt;
    }
    // Perform ECDH.
    crypto::ScopedEC_POINT shared_secret_point = ComputeEcdhSharedSecretPoint(
        *curve, *their_public_key_pt, *private_key_bn);
    if (!shared_secret_point) {
      LOG(ERROR) << "Failed to compute shared secret point.";
      return std::nullopt;
    }
    if (!ComputeEcdhSharedSecret(*curve, *shared_secret_point, &dh_secret)) {
      LOG(ERROR) << "Failed to compute shared secret.";
      return std::nullopt;
    }
    hkdf_info = brillo::BlobFromString(kHkdfInfoWithPublicKey);
    std::optional<brillo::Blob> our_public_key_opt =
        EncodePublicKey(*curve, context.get(), *public_key_pt);
    if (!our_public_key_opt.has_value()) {
      LOG(ERROR) << "Failed to encode public key.";
      return std::nullopt;
    }
    our_public_key = std::move(*our_public_key_opt);
  }

  brillo::SecureBlob keying_material =
      brillo::SecureBlob::Combine(dh_secret, shared_secret);
  brillo::Blob salt =
      brillo::CombineBlobs({brillo::BlobFromString(kHkdfSaltPrefix),
                            brillo::Blob(std::begin(kSecureBoxVersion),
                                         std::end(kSecureBoxVersion))});
  brillo::SecureBlob secret_key;
  if (!Hkdf(HkdfHash::kSha256, keying_material, hkdf_info, salt, kAES128KeySize,
            &secret_key)) {
    LOG(ERROR) << "Failed to perform HKDF.";
    return std::nullopt;
  }
  brillo::Blob nonce, tag, ciphertext;
  std::optional<brillo::SecureBlob> ad;
  if (!header.empty()) {
    ad = brillo::SecureBlob(header.begin(), header.end());
  }
  if (!AesGcmEncrypt(payload, ad, secret_key, &nonce, &tag, &ciphertext)) {
    LOG(ERROR) << "Failed to perform AES-GCM.";
    return std::nullopt;
  }
  brillo::Blob encrypted_payload = brillo::CombineBlobs(
      {brillo::Blob(std::begin(kSecureBoxVersion), std::end(kSecureBoxVersion)),
       our_public_key, nonce, ciphertext, tag});
  return encrypted_payload;
}

}  // namespace secure_box
}  // namespace hwsec_foundation
