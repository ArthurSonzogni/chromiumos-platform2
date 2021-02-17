// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/openssl_util.h"

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <openssl/err.h>

namespace cryptohome {

ScopedBN_CTX CreateBigNumContext() {
  ScopedBN_CTX bn_ctx(BN_CTX_new());
  if (!bn_ctx) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  return bn_ctx;
}

crypto::ScopedBIGNUM CreateBigNum() {
  crypto::ScopedBIGNUM result(BN_secure_new());
  if (!result) {
    LOG(ERROR) << "Failed to allocate BIGNUM structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  return result;
}

crypto::ScopedBIGNUM BigNumFromValue(BN_ULONG value) {
  crypto::ScopedBIGNUM result(BN_secure_new());
  if (!result) {
    LOG(ERROR) << "Failed to allocate BIGNUM structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  if (BN_set_word(result.get(), value) != 1) {
    LOG(ERROR) << "Failed to allocate BIGNUM structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  return result;
}

crypto::ScopedBIGNUM SecureBlobToBigNum(const brillo::SecureBlob& blob) {
  crypto::ScopedBIGNUM result(BN_secure_new());
  if (!result) {
    LOG(ERROR) << "Failed to allocate BIGNUM structure: " << GetOpenSSLErrors();
    return nullptr;
  }
  if (!BN_bin2bn(blob.data(), blob.size(), result.get())) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM: "
               << GetOpenSSLErrors();
    return nullptr;
  }
  return result;
}

bool BigNumToSecureBlob(const BIGNUM& bn, brillo::SecureBlob* result) {
  result->resize(BN_num_bytes(&bn));
  if (BN_bn2bin(&bn, result->data()) < 0) {
    LOG(ERROR) << "Failed to convert BIGNUM to SecureBlob: "
               << GetOpenSSLErrors();
    return false;
  }
  return true;
}

std::string GetOpenSSLErrors() {
  std::string message;
  int error_code;
  while ((error_code = ERR_get_error()) != 0) {
    char error_buf[256];
    error_buf[0] = 0;
    ERR_error_string_n(error_code, error_buf, sizeof(error_buf));
    base::StrAppend(&message, {error_buf, ";"});
  }
  return message;
}

}  // namespace cryptohome
