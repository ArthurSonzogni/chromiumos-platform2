// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/crypto/secure_box.h"

#include <optional>

#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec-foundation/crypto/big_num_util.h"
#include "libhwsec-foundation/crypto/elliptic_curve.h"

namespace hwsec_foundation {

TEST(SecureBoxTest, DeriveKeyPairFromSeed) {
  brillo::SecureBlob seed;
  EXPECT_TRUE(brillo::SecureBlob::HexStringToSecureBlob("DEADBEEF", &seed));
  std::optional<secure_box::KeyPair> key_pair =
      secure_box::DeriveKeyPairFromSeed(seed);
  ASSERT_TRUE(key_pair.has_value());
  EXPECT_EQ(
      base::HexEncode(key_pair->public_key.data(), key_pair->public_key.size()),
      "0492B36A9A2FCF1398328C3E6ECA6D5D3D952930E8833319167A31BF3313CA15BD"
      "D9B29C4D323062BD23330CBF58631116C5373FF5A90D791DBB197E56A6FF49B3");
  EXPECT_EQ(base::HexEncode(key_pair->private_key.data(),
                            key_pair->private_key.size()),
            "00000000000000000000000000000000000000000000000000000000DEADBEF004"
            "92B36A9A2FCF1398328C3E6ECA6D5D3D952930E8833319167A31BF3313CA15BDD9"
            "B29C4D323062BD23330CBF58631116C5373FF5A90D791DBB197E56A6FF49B3");
}

TEST(SecureBoxTest, Encrypt) {
  const brillo::SecureBlob kSharedSecret("abcd1234");
  const brillo::Blob kHeader = brillo::BlobFromString("header");
  const brillo::SecureBlob kPlaintext("secret_message");
  brillo::SecureBlob seed;
  EXPECT_TRUE(brillo::SecureBlob::HexStringToSecureBlob("DEADBEEF", &seed));
  std::optional<secure_box::KeyPair> key_pair =
      secure_box::DeriveKeyPairFromSeed(seed);
  ASSERT_TRUE(key_pair.has_value());
  std::optional<brillo::Blob> encrypted = secure_box::Encrypt(
      key_pair->public_key, kSharedSecret, kHeader, kPlaintext);
  ASSERT_TRUE(encrypted.has_value());
  // Version (2 bytes) + public key + nonce (16 bytes) + ciphertext + IV (12
  // bytes)
  EXPECT_EQ(encrypted->size(), 2 + 65 + 16 + kPlaintext.size() + 12);
}

TEST(SecureBoxTest, EncryptAsymmetricOnly) {
  const brillo::Blob kHeader = brillo::BlobFromString("header");
  const brillo::SecureBlob kPlaintext("secret_message");
  brillo::SecureBlob seed;
  EXPECT_TRUE(brillo::SecureBlob::HexStringToSecureBlob("DEADBEEF", &seed));
  std::optional<secure_box::KeyPair> key_pair =
      secure_box::DeriveKeyPairFromSeed(seed);
  ASSERT_TRUE(key_pair.has_value());
  std::optional<brillo::Blob> encrypted = secure_box::Encrypt(
      key_pair->public_key, brillo::SecureBlob(), kHeader, kPlaintext);
  ASSERT_TRUE(encrypted.has_value());
  // Version (2 bytes) + public key + nonce (16 bytes) + ciphertext + IV (12
  // bytes)
  EXPECT_EQ(encrypted->size(), 2 + 65 + 16 + kPlaintext.size() + 12);
}

TEST(SecureBoxTest, EncryptSymmetricOnly) {
  const brillo::SecureBlob kSharedSecret("abcd1234");
  const brillo::Blob kHeader = brillo::BlobFromString("header");
  const brillo::SecureBlob kPlaintext("secret_message");
  std::optional<brillo::Blob> encrypted =
      secure_box::Encrypt(brillo::Blob(), kSharedSecret, kHeader, kPlaintext);
  ASSERT_TRUE(encrypted.has_value());
  // Version (2 bytes) + public key + nonce (16 bytes) + ciphertext + IV (12
  // bytes)
  EXPECT_EQ(encrypted->size(), 2 + 16 + kPlaintext.size() + 12);
}

// Pubic key and shared secret can not both be empty.
TEST(SecureBoxTest, EncryptInvalidParams) {
  const brillo::Blob kHeader = brillo::BlobFromString("header");
  const brillo::SecureBlob kPlaintext("secret_message");
  std::optional<brillo::Blob> encrypted = secure_box::Encrypt(
      brillo::Blob(), brillo::SecureBlob(), kHeader, kPlaintext);
  EXPECT_FALSE(encrypted.has_value());
}

TEST(SecureBoxTest, EncodePublicKey) {
  ScopedBN_CTX context = CreateBigNumContext();
  ASSERT_TRUE(context);
  std::optional<EllipticCurve> curve =
      EllipticCurve::Create(EllipticCurve::CurveType::kPrime256, context.get());
  ASSERT_TRUE(curve.has_value());
  crypto::ScopedBIGNUM scalar = curve->RandomNonZeroScalar(context.get());
  ASSERT_TRUE(scalar);
  crypto::ScopedEC_POINT point =
      curve->MultiplyWithGenerator(*scalar, context.get());
  ASSERT_TRUE(point);
  std::optional<brillo::Blob> encoded =
      secure_box::EncodePublicKey(*curve, context.get(), *point);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ(encoded->size(), 65);
}

}  // namespace hwsec_foundation
