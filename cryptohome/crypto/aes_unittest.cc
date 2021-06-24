// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/aes.h"

#include <string>

#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/crypto/secure_blob_util.h"

namespace cryptohome {

// This is not a known vector but a very simple test against the API.
TEST(AesTest, AesGcmTestSimple) {
  brillo::SecureBlob key(kAesGcm256KeySize);
  brillo::SecureBlob iv(kAesGcmIVSize);
  brillo::SecureBlob tag(kAesGcmTagSize);

  brillo::SecureBlob ciphertext(4096, '\0');

  std::string message = "I am encrypting this message.";
  brillo::SecureBlob plaintext(message.begin(), message.end());

  GetSecureRandom(key.data(), key.size());

  EXPECT_TRUE(AesGcmEncrypt(plaintext, /*ad=*/base::nullopt, key, &iv, &tag,
                            &ciphertext));

  // Validity check that the encryption actually did something.
  EXPECT_NE(ciphertext, plaintext);
  EXPECT_EQ(ciphertext.size(), plaintext.size());

  brillo::SecureBlob decrypted_plaintext(4096);
  EXPECT_TRUE(AesGcmDecrypt(ciphertext, /*ad=*/base::nullopt, tag, key, iv,
                            &decrypted_plaintext));

  EXPECT_EQ(plaintext, decrypted_plaintext);
}

TEST(AesTest, AesGcmTestWithAd) {
  brillo::SecureBlob key(kAesGcm256KeySize);
  brillo::SecureBlob iv(kAesGcmIVSize);
  brillo::SecureBlob tag(kAesGcmTagSize);

  brillo::SecureBlob ciphertext(4096, '\0');

  std::string message = "I am encrypting this message.";
  brillo::SecureBlob plaintext(message.begin(), message.end());

  std::string ad_value = "This is authentication data.";
  brillo::SecureBlob ad(ad_value.begin(), ad_value.end());

  GetSecureRandom(key.data(), key.size());

  EXPECT_TRUE(AesGcmEncrypt(plaintext, ad, key, &iv, &tag, &ciphertext));

  // Validity check that the encryption actually did something.
  EXPECT_NE(ciphertext, plaintext);
  EXPECT_EQ(ciphertext.size(), plaintext.size());

  brillo::SecureBlob decrypted_plaintext(4096);
  EXPECT_TRUE(
      AesGcmDecrypt(ciphertext, ad, tag, key, iv, &decrypted_plaintext));

  EXPECT_EQ(plaintext, decrypted_plaintext);
}

TEST(AesTest, AesGcmTestWrongAd) {
  brillo::SecureBlob key(kAesGcm256KeySize);
  brillo::SecureBlob iv(kAesGcmIVSize);
  brillo::SecureBlob tag(kAesGcmTagSize);

  brillo::SecureBlob ciphertext(4096, '\0');

  std::string message = "I am encrypting this message.";
  brillo::SecureBlob plaintext(message.begin(), message.end());

  std::string ad_value = "This is authentication data.";
  brillo::SecureBlob ad(ad_value.begin(), ad_value.end());

  GetSecureRandom(key.data(), key.size());

  EXPECT_TRUE(AesGcmEncrypt(plaintext, ad, key, &iv, &tag, &ciphertext));

  // Validity check that the encryption actually did something.
  EXPECT_NE(ciphertext, plaintext);
  EXPECT_EQ(ciphertext.size(), plaintext.size());

  std::string new_ad_value = "Wrong authentication data.";
  brillo::SecureBlob new_ad(new_ad_value.begin(), new_ad_value.end());
  {
    brillo::SecureBlob decrypted_plaintext(4096);
    EXPECT_FALSE(
        AesGcmDecrypt(ciphertext, new_ad, tag, key, iv, &decrypted_plaintext));
    EXPECT_NE(plaintext, decrypted_plaintext);
  }
  {
    brillo::SecureBlob decrypted_plaintext(4096);
    EXPECT_FALSE(AesGcmDecrypt(ciphertext, /*ad=*/base::nullopt, tag, key, iv,
                               &decrypted_plaintext));
    EXPECT_NE(plaintext, decrypted_plaintext);
  }
}

TEST(AesTest, AesGcmTestWrongKey) {
  brillo::SecureBlob key(kAesGcm256KeySize);
  brillo::SecureBlob iv(kAesGcmIVSize);
  brillo::SecureBlob tag(kAesGcmTagSize);

  brillo::SecureBlob ciphertext(4096, '\0');

  std::string message = "I am encrypting this message.";
  brillo::SecureBlob plaintext(message.begin(), message.end());

  GetSecureRandom(key.data(), key.size());

  EXPECT_TRUE(AesGcmEncrypt(plaintext, /*ad=*/base::nullopt, key, &iv, &tag,
                            &ciphertext));

  // Validity check that the encryption actually did something.
  EXPECT_NE(ciphertext, plaintext);
  EXPECT_EQ(ciphertext.size(), plaintext.size());

  brillo::SecureBlob wrong_key(kAesGcm256KeySize);
  GetSecureRandom(wrong_key.data(), wrong_key.size());

  brillo::SecureBlob decrypted_plaintext(4096);
  EXPECT_FALSE(AesGcmDecrypt(ciphertext, /*ad=*/base::nullopt, tag, wrong_key,
                             iv, &decrypted_plaintext));
  EXPECT_NE(plaintext, decrypted_plaintext);
}

TEST(AesTest, AesGcmTestWrongIV) {
  brillo::SecureBlob key(kAesGcm256KeySize);
  brillo::SecureBlob iv(kAesGcmIVSize);
  brillo::SecureBlob tag(kAesGcmTagSize);

  brillo::SecureBlob ciphertext(4096, '\0');

  std::string message = "I am encrypting this message.";
  brillo::SecureBlob plaintext(message.begin(), message.end());

  GetSecureRandom(key.data(), key.size());

  EXPECT_TRUE(AesGcmEncrypt(plaintext, /*ad=*/base::nullopt, key, &iv, &tag,
                            &ciphertext));

  // Validity check that the encryption actually did something.
  EXPECT_NE(ciphertext, plaintext);
  EXPECT_EQ(ciphertext.size(), plaintext.size());

  brillo::SecureBlob wrong_iv(kAesGcmIVSize);
  GetSecureRandom(wrong_iv.data(), wrong_iv.size());

  brillo::SecureBlob decrypted_plaintext(4096);
  EXPECT_FALSE(AesGcmDecrypt(ciphertext, /*ad=*/base::nullopt, tag, key,
                             wrong_iv, &decrypted_plaintext));
  EXPECT_NE(plaintext, decrypted_plaintext);
}

TEST(AesTest, AesGcmTestWrongTag) {
  brillo::SecureBlob key(kAesGcm256KeySize);
  brillo::SecureBlob iv(kAesGcmIVSize);
  brillo::SecureBlob tag(kAesGcmTagSize);

  brillo::SecureBlob ciphertext(4096, '\0');

  std::string message = "I am encrypting this message.";
  brillo::SecureBlob plaintext(message.begin(), message.end());

  GetSecureRandom(key.data(), key.size());

  EXPECT_TRUE(AesGcmEncrypt(plaintext, /*ad=*/base::nullopt, key, &iv, &tag,
                            &ciphertext));

  // Validity check that the encryption actually did something.
  EXPECT_NE(ciphertext, plaintext);
  EXPECT_EQ(ciphertext.size(), plaintext.size());

  brillo::SecureBlob wrong_tag(kAesGcmTagSize);
  GetSecureRandom(wrong_tag.data(), wrong_tag.size());

  brillo::SecureBlob decrypted_plaintext(4096);
  EXPECT_FALSE(AesGcmDecrypt(ciphertext, /*ad=*/base::nullopt, wrong_tag, key,
                             iv, &decrypted_plaintext));
}

// This tests that AesGcmEncrypt produces a different IV on subsequent runs.
// Note that this is in no way a mathematical test of secure randomness. It
// makes sure nobody in the future, for some reason, changes AesGcmEncrypt to
// use a fixed IV without tests failing, at which point they will find this
// test, and see that AesGcmEncrypt *must* return random IVs.
TEST(AesTest, AesGcmTestUniqueIVs) {
  brillo::SecureBlob key(kAesGcm256KeySize);
  brillo::SecureBlob tag(kAesGcmTagSize);

  brillo::SecureBlob ciphertext(4096, '\0');

  std::string message = "I am encrypting this message.";
  brillo::SecureBlob plaintext(message.begin(), message.end());

  GetSecureRandom(key.data(), key.size());

  brillo::SecureBlob iv(kAesGcmIVSize);
  EXPECT_TRUE(AesGcmEncrypt(plaintext, /*ad=*/base::nullopt, key, &iv, &tag,
                            &ciphertext));

  brillo::SecureBlob iv2(kAesGcmIVSize);
  EXPECT_TRUE(AesGcmEncrypt(plaintext, /*ad=*/base::nullopt, key, &iv2, &tag,
                            &ciphertext));

  brillo::SecureBlob iv3(kAesGcmIVSize);
  EXPECT_TRUE(AesGcmEncrypt(plaintext, /*ad=*/base::nullopt, key, &iv3, &tag,
                            &ciphertext));

  EXPECT_NE(iv, iv2);
  EXPECT_NE(iv, iv3);
}

}  // namespace cryptohome
