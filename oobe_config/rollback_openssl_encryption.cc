// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_openssl_encryption.h"

#include <string>

#include <base/optional.h>
#include <base/strings/strcat.h>
#include <brillo/secure_blob.h>
#include <openssl/rand.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/evp.h>

namespace oobe_config {

namespace {

constexpr int kIvSize = 12;
constexpr int kKeySize = 32;
constexpr int kTagSize = 16;

base::Optional<brillo::SecureBlob> GenerateRandomKey() {
  brillo::SecureBlob key(kKeySize);
  if (!RAND_bytes(key.data(), kKeySize)) {
    return base::nullopt;
  }
  return key;
}

// Generates a random initialization vector.
base::Optional<brillo::Blob> GenerateRandomIV() {
  brillo::Blob iv(kIvSize);
  if (!RAND_bytes(iv.data(), kIvSize)) {
    return base::nullopt;
  }
  return iv;
}

}  // namespace

base::Optional<EncryptedData> Encrypt(const brillo::SecureBlob& data) {
  base::Optional<brillo::SecureBlob> key = GenerateRandomKey();
  base::Optional<brillo::Blob> iv = GenerateRandomIV();

  if (!iv.has_value() || !key.has_value()) {
    return base::nullopt;
  }

  DCHECK_EQ(iv->size(), kIvSize);
  DCHECK_EQ(key->size(), kKeySize);

  crypto::ScopedEVP_CIPHER_CTX context(EVP_CIPHER_CTX_new());
  if (!EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr,
                          key->data(), iv->data())) {
    return base::nullopt;
  }

  brillo::Blob encrypted(data.size());
  int encrypted_length = 0;
  if (!EVP_EncryptUpdate(context.get(), encrypted.data(), &encrypted_length,
                         data.data(), data.size())) {
    return base::nullopt;
  }

  DCHECK_EQ(encrypted_length, data.size());

  if (!EVP_EncryptFinal_ex(context.get(), nullptr, &encrypted_length)) {
    return base::nullopt;
  }

  DCHECK_EQ(encrypted_length, 0);

  brillo::Blob tag(kTagSize);
  if (!EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, kTagSize,
                           tag.data())) {
    return base::nullopt;
  }

  encrypted.insert(encrypted.end(), tag.begin(), tag.end());
  encrypted.insert(encrypted.end(), iv->begin(), iv->end());
  return {{encrypted, *key}};
}

base::Optional<brillo::SecureBlob> Decrypt(
    const EncryptedData& encrypted_data) {
  const brillo::Blob& input = encrypted_data.data;

  CHECK_GE(input.size(), kTagSize + kIvSize);
  CHECK_EQ(encrypted_data.key.size(), kKeySize);

  brillo::Blob encrypted(input.begin(), input.end() - kTagSize - kIvSize);
  brillo::Blob tag(input.begin() + encrypted.size(), input.end() - kIvSize);
  brillo::Blob iv(input.begin() + encrypted.size() + kTagSize, input.end());

  crypto::ScopedEVP_CIPHER_CTX context(EVP_CIPHER_CTX_new());

  if (!EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr,
                          encrypted_data.key.data(), iv.data())) {
    return base::nullopt;
  }

  brillo::SecureBlob output(encrypted.size());
  int output_length;
  if (!EVP_DecryptUpdate(context.get(), output.data(), &output_length,
                         encrypted.data(), encrypted.size())) {
    return base::nullopt;
  }

  DCHECK_EQ(output_length, output.size());

  if (!EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, tag.size(),
                           tag.data())) {
    return base::nullopt;
  }

  if (!EVP_DecryptFinal_ex(context.get(), nullptr, &output_length)) {
    return base::nullopt;
  }

  DCHECK_EQ(output_length, 0);

  return output;
}

}  // namespace oobe_config
