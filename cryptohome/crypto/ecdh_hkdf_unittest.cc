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

// Use the same test vectors as in Tink library to ensure key generation methods
// are equivalent:
// https://github.com/google/tink/blob/1.5/cc/subtle/ecies_hkdf_recipient_kem_boringssl_test.cc
static const char kSaltHex[] = "0b0b0b0b";
static const char kInfoHex[] = "0b0b0b0b0b0b0b0b";
static const char kNistP256PublicValueHex[] =
    "04700c48f77f56584c5cc632ca65640db91b6bacce3a4df6b42ce7cc838833d287db71e509"
    "e3fd9b060ddb20ba5c51dcc5948d46fbf640dfe0441782cab85fa4ac";
static const char kNistP256PrivateKeyHex[] =
    "7d7dc5f71eb29ddaf80d6214632eeae03d9058af1fb6d22ed80badb62bc1a534";
static const char kNistP256SharedKeyHex[] =
    "0f19c0f322fc0a4b73b32bac6a66baa274de261db38a57f11ee4896ede24dbba";

}  // namespace

TEST(EcdhHkdfTest, GenerateEcdhHkdfRecipientKey) {
  ScopedBN_CTX context = CreateBigNumContext();
  ASSERT_TRUE(context);
  base::Optional<EllipticCurve> ec =
      EllipticCurve::Create(kCurve, context.get());
  ASSERT_TRUE(ec);

  brillo::SecureBlob info;
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kInfoHex, &info));
  brillo::SecureBlob salt;
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kSaltHex, &salt));

  brillo::SecureBlob rec_priv_key;
  brillo::SecureBlob eph_pub_key;
  brillo::SecureBlob shared_key;
  brillo::SecureBlob expected_shared_key;
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kNistP256PublicValueHex,
                                                        &eph_pub_key));
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kNistP256PrivateKeyHex,
                                                        &rec_priv_key));
  ASSERT_TRUE(brillo::SecureBlob::HexStringToSecureBlob(kNistP256SharedKeyHex,
                                                        &expected_shared_key));
  ASSERT_TRUE(GenerateEcdhHkdfRecipientKey(*ec, rec_priv_key, eph_pub_key, info,
                                           salt, kHkdfHash, kEcdhHkdfKeySize,
                                           &shared_key));
  EXPECT_EQ(expected_shared_key, shared_key);
}

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
  EXPECT_TRUE(AesGcmEncrypt(plaintext, aes_gcm_key1, &iv, &tag, &ciphertext));

  ASSERT_TRUE(GenerateEcdhHkdfRecipientKey(*ec, rec_priv_key, eph_pub_key, info,
                                           salt, kHkdfHash, kEcdhHkdfKeySize,
                                           &aes_gcm_key2));

  // Symmetric keys generated for sender and recipient should be equal.
  EXPECT_EQ(aes_gcm_key1, aes_gcm_key2);

  EXPECT_NE(ciphertext, plaintext);
  EXPECT_EQ(ciphertext.size(), plaintext.size());

  // Decrypt using recipient's `aes_gcm_key2`.
  brillo::SecureBlob decrypted_plaintext;
  EXPECT_TRUE(
      AesGcmDecrypt(ciphertext, tag, aes_gcm_key2, iv, &decrypted_plaintext));

  EXPECT_EQ(plaintext, decrypted_plaintext);
}

}  // namespace cryptohome
