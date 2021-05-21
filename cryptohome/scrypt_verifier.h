// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SCRYPT_VERIFIER_H_
#define CRYPTOHOME_SCRYPT_VERIFIER_H_

#include <brillo/secure_blob.h>

#include <cryptohome/credential_verifier.h>

namespace cryptohome {

class ScryptVerifier final : public CredentialVerifier {
 public:
  ScryptVerifier() = default;
  ~ScryptVerifier() override = default;

  // Prohibit copy/move/assignment.
  ScryptVerifier(const ScryptVerifier&) = delete;
  ScryptVerifier(const ScryptVerifier&&) = delete;
  ScryptVerifier& operator=(const ScryptVerifier&) = delete;
  ScryptVerifier& operator=(const ScryptVerifier&&) = delete;

  bool Set(const brillo::SecureBlob& secret) override;
  bool Verify(const brillo::SecureBlob& secret) override;

 private:
  brillo::SecureBlob scrypt_salt_;
  brillo::SecureBlob verifier_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SCRYPT_VERIFIER_H_
