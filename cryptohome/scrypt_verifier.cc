// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/scrypt_verifier.h"

#include <brillo/secure_blob.h>

#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto/secure_blob_util.h"

namespace cryptohome {

namespace {

constexpr int kScryptNFactor = 1 << 12;  // 2^12
constexpr int kScryptRFactor = 8;
constexpr int kScryptPFactor = 1;
constexpr int kScryptSaltSize = 256 / CHAR_BIT;
constexpr int kScryptOutputSize = 256 / CHAR_BIT;

}  // namespace

bool ScryptVerifier::Set(const brillo::SecureBlob& secret) {
  verifier_.clear();
  verifier_.resize(kScryptOutputSize, 0);
  scrypt_salt_ = CreateSecureRandomBlob(kScryptSaltSize);

  return Scrypt(secret, scrypt_salt_, kScryptNFactor, kScryptRFactor,
                kScryptPFactor, &verifier_);
}

bool ScryptVerifier::Verify(const brillo::SecureBlob& secret) {
  brillo::SecureBlob hashed_secret(kScryptOutputSize, 0);
  if (!Scrypt(secret, scrypt_salt_, kScryptNFactor, kScryptRFactor,
              kScryptPFactor, &hashed_secret)) {
    LOG(ERROR) << "Scrypt failed.";
    return false;
  }
  if (verifier_.size() != hashed_secret.size()) {
    return false;
  }
  return (brillo::SecureMemcmp(hashed_secret.data(), verifier_.data(),
                               verifier_.size()) == 0);
}

}  // namespace cryptohome
