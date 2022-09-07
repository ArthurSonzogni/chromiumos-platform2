// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/credential_verifier_factory.h"

#include <memory>

#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/scrypt_verifier.h"

namespace cryptohome {

bool IsCredentialVerifierSupported(AuthFactorType auth_factor_type) {
  switch (auth_factor_type) {
    case AuthFactorType::kPassword:
      return true;
    case AuthFactorType::kPin:
    case AuthFactorType::kCryptohomeRecovery:
    case AuthFactorType::kKiosk:
    case AuthFactorType::kSmartCard:
    case AuthFactorType::kUnspecified:
      return false;
  }
}

std::unique_ptr<CredentialVerifier> CreateCredentialVerifier(
    const brillo::SecureBlob& passkey) {
  auto verifier = std::make_unique<ScryptVerifier>();
  if (!verifier->Set(passkey)) {
    LOG(ERROR) << "Credential verifier initialization failed.";
    return nullptr;
  }
  return verifier;
}

}  // namespace cryptohome
