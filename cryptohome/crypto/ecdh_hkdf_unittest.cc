// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/ecdh_hkdf.h"

#include <gtest/gtest.h>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/big_num_util.h"

namespace cryptohome {

namespace {
constexpr EllipticCurve::CurveType kCurve = EllipticCurve::CurveType::kPrime256;
constexpr HkdfHash kHkdfHash = HkdfHash::kSha256;
const size_t kEcdhHkdfKeySize = kAesGcm256KeySize;

static const char kSaltHex[] = "0b0b0b0b";
static const char kInfoHex[] = "0b0b0b0b0b0b0b0b";
}  // namespace

TEST(EcdhHkdfTest, CompareEcdhHkdfSymmetricKeys) {
  ScopedBN_CTX context = CreateBigNumContext();
  ASSERT_TRUE(context);
  base::Optional<EllipticCurve> ec =
      EllipticCurve::Create(kCurve, context.get());
  ASSERT_TRUE(ec);

  brillo::SecureBlob info;
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kInfoHex, &info));
  brillo::SecureBlob salt;
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kSaltHex, &salt));

  brillo::SecureBlob rec_pub_key;
  brillo::SecureBlob rec_priv_key;
  brillo::SecureBlob eph_pub_key;
  brillo::SecureBlob eph_priv_key;
  brillo::SecureBlob symmetric_key1;
  brillo::SecureBlob symmetric_key2;

  ASSERT_TRUE(ec->GenerateKeysAsSecureBlobs(&rec_pub_key, &rec_priv_key,
                                            context.get()));
  ASSERT_TRUE(ec->GenerateKeysAsSecureBlobs(&eph_pub_key, &eph_priv_key,
                                            context.get()));
  ASSERT_TRUE(GenerateEcdhHkdfSenderKey(*ec, rec_pub_key, eph_pub_key,
                                        eph_priv_key, info, salt, kHkdfHash,
                                        kEcdhHkdfKeySize, &symmetric_key1));
  ASSERT_TRUE(GenerateEcdhHkdfRecipientKey(*ec, rec_priv_key, eph_pub_key, info,
                                           salt, kHkdfHash, kEcdhHkdfKeySize,
                                           &symmetric_key2));

  EXPECT_EQ(symmetric_key1.size(), kAesGcm256KeySize);
  EXPECT_EQ(symmetric_key2.size(), kAesGcm256KeySize);

  // Symmetric keys generated for sender and recipient should be equal.
  EXPECT_EQ(symmetric_key1, symmetric_key2);
}

TEST(EcdhHkdfTest, AesGcmEncryptionDecryption) {
  ScopedBN_CTX context = CreateBigNumContext();
  ASSERT_TRUE(context);
  base::Optional<EllipticCurve> ec =
      EllipticCurve::Create(kCurve, context.get());
  ASSERT_TRUE(ec);

  brillo::SecureBlob info;
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kInfoHex, &info));
  brillo::SecureBlob salt;
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kSaltHex, &salt));

  brillo::SecureBlob rec_pub_key;
  brillo::SecureBlob rec_priv_key;
  brillo::SecureBlob eph_pub_key;
  brillo::SecureBlob eph_priv_key;
  brillo::SecureBlob aes_gcm_key1;
  brillo::SecureBlob aes_gcm_key2;

  ASSERT_TRUE(ec->GenerateKeysAsSecureBlobs(&rec_pub_key, &rec_priv_key,
                                            context.get()));
  ASSERT_TRUE(ec->GenerateKeysAsSecureBlobs(&eph_pub_key, &eph_priv_key,
                                            context.get()));
  ASSERT_TRUE(GenerateEcdhHkdfSenderKey(*ec, rec_pub_key, eph_pub_key,
                                        eph_priv_key, info, salt, kHkdfHash,
                                        kEcdhHkdfKeySize, &aes_gcm_key1));

  brillo::SecureBlob iv(kAesGcmIVSize);
  brillo::SecureBlob tag(kAesGcmTagSize);
  brillo::SecureBlob ciphertext;
  brillo::SecureBlob plaintext("I am encrypting this message.");

  // Encrypt using sender's `aes_gcm_key1`.
  EXPECT_TRUE(AesGcmEncrypt(plaintext, /*ad=*/base::nullopt, aes_gcm_key1, &iv,
                            &tag, &ciphertext));

  ASSERT_TRUE(GenerateEcdhHkdfRecipientKey(*ec, rec_priv_key, eph_pub_key, info,
                                           salt, kHkdfHash, kEcdhHkdfKeySize,
                                           &aes_gcm_key2));

  // Symmetric keys generated for sender and recipient should be equal.
  EXPECT_EQ(aes_gcm_key1, aes_gcm_key2);

  EXPECT_NE(ciphertext, plaintext);
  EXPECT_EQ(ciphertext.size(), plaintext.size());

  // Decrypt using recipient's `aes_gcm_key2`.
  brillo::SecureBlob decrypted_plaintext;
  EXPECT_TRUE(AesGcmDecrypt(ciphertext, /*ad=*/base::nullopt, tag, aes_gcm_key2,
                            iv, &decrypted_plaintext));

  EXPECT_EQ(plaintext, decrypted_plaintext);
}

}  // namespace cryptohome
