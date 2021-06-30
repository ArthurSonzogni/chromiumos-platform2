// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/scrypt.h"

#include <openssl/rsa.h>

#include <base/base64.h>
#include <base/check.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest.h>

#include "cryptohome/crypto/secure_blob_util.h"

using brillo::SecureBlob;

namespace cryptohome {

namespace {

void CheckBlob(const brillo::SecureBlob& original_blob,
               const brillo::SecureBlob& key,
               const brillo::SecureBlob& wrapped_blob,
               const std::string& original_str) {
  brillo::SecureBlob decrypted_blob(wrapped_blob.size());
  CryptoError error;
  EXPECT_TRUE(
      DeprecatedDecryptScryptBlob(wrapped_blob, key, &decrypted_blob, &error));

  const std::string decrypted_str(decrypted_blob.begin(), decrypted_blob.end());
  EXPECT_EQ(original_str, decrypted_str);
}

}  // namespace

// These tests check that DeprecatedEncryptScryptBlob and
// DeprecatedDecryptScryptBlob continue to perform the same function, and
// interoperate correctly, as they are re-written and re-factored. These do not
// prove cryptographic properties of the functions, or formal verification. They
// are validity checks for compatibility.
TEST(ScryptTest, DeprecatedEncryptScrypt) {
  const std::string blob_str = "nOaVD3qRNqWhqQTDgyGb";
  brillo::SecureBlob blob(blob_str.begin(), blob_str.end());
  const std::string key_source_str = "UNdGe2HbyyXqIzpuxhVn";
  brillo::SecureBlob key_source(key_source_str.begin(), key_source_str.end());

  brillo::SecureBlob wrapped_blob;
  EXPECT_TRUE(DeprecatedEncryptScryptBlob(blob, key_source, &wrapped_blob));

  CheckBlob(blob, key_source, wrapped_blob, blob_str);

  brillo::SecureBlob fixed_bytes_blob = {
      0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x00, 0x00, 0x02, 0x96, 0x22, 0x20, 0xd6, 0x95, 0x85, 0x9c, 0x3e,
      0xf0, 0xd4, 0x8f, 0x75, 0x64, 0x67, 0xa5, 0xd3, 0x0a, 0x67, 0xb7, 0xb8,
      0xa1, 0xcf, 0x97, 0xec, 0x6a, 0x34, 0xf5, 0xa6, 0x7e, 0x76, 0x2d, 0xa8,
      0x4f, 0xea, 0x98, 0x03, 0x46, 0xaf, 0x54, 0x1c, 0x1a, 0x5a, 0x65, 0x0b,
      0x65, 0x84, 0xcb, 0x96, 0x4b, 0x81, 0x3f, 0x3d, 0x4a, 0xf6, 0xfe, 0xac,
      0xa2, 0xd0, 0xb4, 0x3f, 0xe7, 0xef, 0x87, 0x00, 0x95, 0x60, 0xb7, 0x92,
      0x4e, 0x44, 0x11, 0x0b, 0xb6, 0xdc, 0x7c, 0x7e, 0x14, 0xa4, 0x59, 0x2d,
      0x24, 0xe7, 0x00, 0x72, 0x2b, 0x35, 0xd3, 0xd2, 0x06, 0xfe, 0xc7, 0x61,
      0x65, 0xfd, 0xa3, 0xe5, 0x7a, 0xed, 0xfd, 0x13, 0x2f, 0x32, 0x4f, 0xa4,
      0x0c, 0x51, 0x40, 0xf4, 0xc5, 0x89, 0x46, 0x79, 0x2c, 0xdb, 0xb8, 0x19,
      0xa3, 0x49, 0x4e, 0x31, 0xd2, 0x09, 0xe8, 0x63, 0x01, 0xdb, 0x7d, 0x43,
      0x54, 0xaa, 0x1e, 0xb3};
  CheckBlob(blob, key_source, fixed_bytes_blob, blob_str);
}

TEST(ScryptTest, DeriveSecretsScrypt) {
  brillo::SecureBlob passkey("passkey");
  brillo::SecureBlob salt("salt");

  const size_t secret_size = 16;
  brillo::SecureBlob result1(secret_size), result2(secret_size),
      result3(secret_size);

  EXPECT_TRUE(
      DeriveSecretsScrypt(passkey, salt, {&result1, &result2, &result3}));

  EXPECT_NE(brillo::SecureBlob(), result1);
  EXPECT_NE(brillo::SecureBlob(), result2);
  EXPECT_NE(brillo::SecureBlob(), result3);
}

TEST(ScryptTest, DeriveSecretsScryptEmptySecrets) {
  brillo::SecureBlob passkey("passkey");
  brillo::SecureBlob salt("salt");

  std::vector<brillo::SecureBlob*> gen_secrets;
  EXPECT_FALSE(DeriveSecretsScrypt(passkey, salt, gen_secrets));

  brillo::SecureBlob empty_blob;
  EXPECT_FALSE(DeriveSecretsScrypt(passkey, salt, {&empty_blob}));
}

}  // namespace cryptohome
