// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_openssl_encryption.h"

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

namespace {

constexpr int kIvSize = 12;
constexpr int kKeySize = 32;
constexpr int kTagSize = 16;

const brillo::SecureBlob kKey(kKeySize, 60);
const brillo::SecureBlob kSensitiveData(859, 61);
const brillo::Blob kData(857, 63);

}  // namespace

namespace oobe_config {

TEST(RollbackOpenSslEncryptionTest, EncryptDecrypt) {
  base::Optional<EncryptedData> encrypted_data = Encrypt(kSensitiveData);
  ASSERT_TRUE(encrypted_data.has_value());

  // Make sure data was changed by encryption.
  auto first_mismatch =
      std::mismatch(std::begin(kSensitiveData), std::end(kSensitiveData),
                    std::begin(encrypted_data->data));
  ASSERT_FALSE(first_mismatch.first == std::end(kSensitiveData));

  base::Optional<brillo::SecureBlob> decrypted_data = Decrypt(*encrypted_data);
  ASSERT_TRUE(decrypted_data.has_value());
  ASSERT_EQ(kSensitiveData, *decrypted_data);
}

TEST(RollbackOpenSslEncryptionTest, EncryptDecryptWithWrongKey) {
  base::Optional<EncryptedData> encrypted_data = Encrypt(kSensitiveData);
  ASSERT_TRUE(encrypted_data.has_value());

  base::Optional<brillo::SecureBlob> decrypted_data =
      Decrypt({encrypted_data->data, kKey});
  ASSERT_FALSE(decrypted_data.has_value());
}

TEST(RollbackOpenSslEncryptionTest, DecryptModifyData) {
  base::Optional<EncryptedData> encrypted_data = Encrypt(kSensitiveData);
  ASSERT_TRUE(encrypted_data.has_value());
  encrypted_data->data[1]++;
  base::Optional<brillo::SecureBlob> decrypted_data =
      Decrypt(encrypted_data.value());
  ASSERT_FALSE(decrypted_data.has_value());
}

TEST(RollbackOpenSslEncryptionTest, DecryptModifyKey) {
  base::Optional<EncryptedData> encrypted_data = Encrypt(kSensitiveData);
  ASSERT_TRUE(encrypted_data.has_value());
  encrypted_data->key[1]++;
  base::Optional<brillo::SecureBlob> decrypted_data =
      Decrypt(encrypted_data.value());
  ASSERT_FALSE(decrypted_data.has_value());
}

TEST(RollbackOpenSslEncryptionTest, DecryptNonesense) {
  base::Optional<brillo::SecureBlob> decrypted_data = Decrypt({kData, kKey});
  ASSERT_FALSE(decrypted_data.has_value());
}

TEST(RollbackOpenSslEncryptionTest, EncryptedDataSize) {
  base::Optional<EncryptedData> encrypted_data = Encrypt(kSensitiveData);
  ASSERT_TRUE(encrypted_data.has_value());

  EXPECT_GE(encrypted_data->data.size(),
            kSensitiveData.size() + kTagSize + kIvSize);
  EXPECT_EQ(encrypted_data->key.size(), kKeySize);
}

}  // namespace oobe_config
