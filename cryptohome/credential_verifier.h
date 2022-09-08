// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIAL_VERIFIER_H_
#define CRYPTOHOME_CREDENTIAL_VERIFIER_H_

#include <brillo/secure_blob.h>

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

  // Returns the type of the factor the verifier was created for.
  AuthFactorType auth_factor_type() const { return auth_factor_type_; }

  // Sets internal state for |secret| Verify().
  virtual bool Set(const brillo::SecureBlob& secret) = 0;

  // Verifies the |secret| against previously Set() state.
  virtual bool Verify(const brillo::SecureBlob& secret) = 0;

 protected:
  explicit CredentialVerifier(AuthFactorType auth_factor_type)
      : auth_factor_type_(auth_factor_type) {}

 private:
  const AuthFactorType auth_factor_type_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIAL_VERIFIER_H_
