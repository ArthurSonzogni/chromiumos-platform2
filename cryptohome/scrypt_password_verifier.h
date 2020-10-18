// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SCRYPT_PASSWORD_VERIFIER_H_
#define CRYPTOHOME_SCRYPT_PASSWORD_VERIFIER_H_

#include <brillo/secure_blob.h>

#include <cryptohome/password_verifier.h>

namespace cryptohome {

class ScryptPasswordVerifier final : public PasswordVerifier {
 public:
  ScryptPasswordVerifier() = default;
  ~ScryptPasswordVerifier() override = default;

  // Prohibit copy/move/assignment.
  ScryptPasswordVerifier(const ScryptPasswordVerifier&) = delete;
  ScryptPasswordVerifier(const ScryptPasswordVerifier&&) = delete;
  ScryptPasswordVerifier& operator=(const ScryptPasswordVerifier&) = delete;
  ScryptPasswordVerifier& operator=(const ScryptPasswordVerifier&&) = delete;

  bool Set(const brillo::SecureBlob& secret) override;
  bool Verify(const brillo::SecureBlob& secret) override;

 private:
  brillo::SecureBlob scrypt_salt_;
  brillo::SecureBlob verifier_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SCRYPT_PASSWORD_VERIFIER_H_
