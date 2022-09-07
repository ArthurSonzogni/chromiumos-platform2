// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIAL_VERIFIER_FACTORY_H_
#define CRYPTOHOME_CREDENTIAL_VERIFIER_FACTORY_H_

#include <memory>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credential_verifier.h"

namespace cryptohome {

// Returns whether a credential verifier is supported for the given factor.
bool IsCredentialVerifierSupported(AuthFactorType auth_factor_type);

// Creates a credential verifier for the given credential.
// TODO(b/204482221): Pass `AuthFactorType` and return null for unsupported
// factors.
std::unique_ptr<CredentialVerifier> CreateCredentialVerifier(
    const brillo::SecureBlob& passkey);

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIAL_VERIFIER_FACTORY_H_
