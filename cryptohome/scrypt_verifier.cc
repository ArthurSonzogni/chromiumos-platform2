// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/scrypt_verifier.h"

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "base/memory/ptr_util.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::Scrypt;

namespace cryptohome {
namespace {

constexpr int kScryptNFactor = 1 << 12;  // 2^12
constexpr int kScryptRFactor = 8;
constexpr int kScryptPFactor = 1;
constexpr int kScryptSaltSize = 256 / CHAR_BIT;
constexpr int kScryptOutputSize = 256 / CHAR_BIT;

}  // namespace

std::unique_ptr<ScryptVerifier> ScryptVerifier::Create(
    std::string auth_factor_label, const brillo::SecureBlob& passkey) {
  // Create a salt and try to scrypt the passkey with it.
  brillo::SecureBlob scrypt_salt = CreateSecureRandomBlob(kScryptSaltSize);
  brillo::SecureBlob verifier(kScryptOutputSize, 0);
  if (Scrypt(passkey, scrypt_salt, kScryptNFactor, kScryptRFactor,
             kScryptPFactor, &verifier)) {
    return base::WrapUnique(new ScryptVerifier(std::move(auth_factor_label),
                                               std::move(scrypt_salt),
                                               std::move(verifier)));
  }
  // If the Scrypt failed, then we can't make a verifier with this passkey.
  return nullptr;
}

bool ScryptVerifier::Verify(const brillo::SecureBlob& secret) const {
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

ScryptVerifier::ScryptVerifier(std::string auth_factor_label,
                               brillo::SecureBlob scrypt_salt,
                               brillo::SecureBlob verifier)
    : CredentialVerifier(AuthFactorType::kPassword,
                         std::move(auth_factor_label),
                         {.metadata = PasswordAuthFactorMetadata()}),
      scrypt_salt_(std::move(scrypt_salt)),
      verifier_(std::move(verifier)) {}

}  // namespace cryptohome
