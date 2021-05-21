// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIAL_VERIFIER_H_
#define CRYPTOHOME_CREDENTIAL_VERIFIER_H_

#include <brillo/secure_blob.h>

namespace cryptohome {

class CredentialVerifier {
 public:
  CredentialVerifier() = default;
  virtual ~CredentialVerifier() = default;

  // Prohibit copy/move/assignment.
  CredentialVerifier(const CredentialVerifier&) = delete;
  CredentialVerifier(const CredentialVerifier&&) = delete;
  CredentialVerifier& operator=(const CredentialVerifier&) = delete;
  CredentialVerifier& operator=(const CredentialVerifier&&) = delete;

  // Sets internal state for |secret| Verify().
  virtual bool Set(const brillo::SecureBlob& secret) = 0;

  // Verifies the |secret| against previously Set() state.
  virtual bool Verify(const brillo::SecureBlob& secret) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIAL_VERIFIER_H_
