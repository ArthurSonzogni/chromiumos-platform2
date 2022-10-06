// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIAL_VERIFIER_H_
#define CRYPTOHOME_CREDENTIAL_VERIFIER_H_

#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

class CredentialVerifier {
 public:
  virtual ~CredentialVerifier() = default;

  // Prohibit copy/move/assignment.
  CredentialVerifier(const CredentialVerifier&) = delete;
  CredentialVerifier(CredentialVerifier&&) = delete;
  CredentialVerifier& operator=(const CredentialVerifier&) = delete;
  CredentialVerifier& operator=(CredentialVerifier&&) = delete;

  // Accessors for the properties of the factor the verifier was created for.
  AuthFactorType auth_factor_type() const { return auth_factor_type_; }
  const std::string& auth_factor_label() const { return auth_factor_label_; }
  const AuthFactorMetadata& auth_factor_metadata() const {
    return auth_factor_metadata_;
  }

  // Sets internal state for |secret| Verify().
  virtual bool Set(const brillo::SecureBlob& secret) = 0;

  // Verifies the |secret| against previously Set() state.
  virtual bool Verify(const brillo::SecureBlob& secret) const = 0;

 protected:
  CredentialVerifier(AuthFactorType auth_factor_type,
                     const std::string& auth_factor_label,
                     const AuthFactorMetadata& auth_factor_metadata)
      : auth_factor_type_(auth_factor_type),
        auth_factor_label_(auth_factor_label),
        auth_factor_metadata_(auth_factor_metadata) {}

 private:
  const AuthFactorType auth_factor_type_;
  const std::string auth_factor_label_;
  const AuthFactorMetadata auth_factor_metadata_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIAL_VERIFIER_H_
