// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/credential_verifier_factory.h"

#include <memory>
#include <optional>
#include <string>

#include <base/check_op.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/key_objects.h"
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
    case AuthFactorType::kLegacyFingerprint:
    case AuthFactorType::kUnspecified:
      return false;
  }
}

std::unique_ptr<CredentialVerifier> CreateCredentialVerifier(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input) {
  if (!IsCredentialVerifierSupported(auth_factor_type)) {
    return nullptr;
  }

  std::unique_ptr<CredentialVerifier> verifier;
  switch (auth_factor_type) {
    case AuthFactorType::kPassword: {
      if (!auth_input.user_input.has_value()) {
        LOG(ERROR) << "Cannot construct a password verifier without a password";
        return nullptr;
      }
      verifier = std::make_unique<ScryptVerifier>(auth_factor_label);
      if (!verifier->Set(*auth_input.user_input)) {
        LOG(ERROR) << "Credential verifier initialization failed.";
        return nullptr;
      }
      break;
    }
    case AuthFactorType::kPin:
    case AuthFactorType::kCryptohomeRecovery:
    case AuthFactorType::kKiosk:
    case AuthFactorType::kSmartCard:
    case AuthFactorType::kLegacyFingerprint:
    case AuthFactorType::kUnspecified: {
      return nullptr;
    }
  }

  DCHECK_EQ(verifier->auth_factor_label(), auth_factor_label);
  DCHECK_EQ(verifier->auth_factor_type(), auth_factor_type);
  return verifier;
}

}  // namespace cryptohome
