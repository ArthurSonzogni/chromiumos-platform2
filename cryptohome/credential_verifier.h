// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIAL_VERIFIER_H_
#define CRYPTOHOME_CREDENTIAL_VERIFIER_H_

#include <string>
#include <utility>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

class CredentialVerifier {
 public:
  CredentialVerifier(AuthFactorType auth_factor_type,
                     std::string auth_factor_label,
                     AuthFactorMetadata auth_factor_metadata)
      : auth_factor_type_(auth_factor_type),
        auth_factor_label_(std::move(auth_factor_label)),
        auth_factor_metadata_(std::move(auth_factor_metadata)) {}

  CredentialVerifier(const CredentialVerifier&) = delete;
  CredentialVerifier& operator=(const CredentialVerifier&) = delete;

  virtual ~CredentialVerifier() = default;

  // Accessors for the properties of the factor the verifier was created for.
  AuthFactorType auth_factor_type() const { return auth_factor_type_; }
  const std::string& auth_factor_label() const { return auth_factor_label_; }
  const AuthFactorMetadata& auth_factor_metadata() const {
    return auth_factor_metadata_;
  }

  // Verifies the |secret| against previously Set() state.
  virtual bool Verify(const brillo::SecureBlob& secret) const = 0;

 private:
  const AuthFactorType auth_factor_type_;
  const std::string auth_factor_label_;
  const AuthFactorMetadata auth_factor_metadata_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIAL_VERIFIER_H_
