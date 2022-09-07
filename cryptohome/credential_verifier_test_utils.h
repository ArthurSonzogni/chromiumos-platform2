// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIAL_VERIFIER_TEST_UTILS_H_
#define CRYPTOHOME_CREDENTIAL_VERIFIER_TEST_UTILS_H_

#include <base/strings/strcat.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/credential_verifier.h"

namespace cryptohome {

// Matches a `CredentialVerifier` pointer that accepts the specified password.
MATCHER_P(IsVerifierPtrForPassword,
          expected_password,
          base::StrCat({"CredentialVerifier that ",
                        negation ? "doesn't accept" : "accepts", " password ",
                        ::testing::PrintToString(expected_password)})) {
  return arg && arg->Verify(brillo::SecureBlob(expected_password));
}

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIAL_VERIFIER_TEST_UTILS_H_
