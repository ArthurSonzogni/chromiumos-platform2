// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/credential_verifier.h"
#include "cryptohome/credential_verifier_factory.h"

namespace cryptohome {
namespace {

TEST(CredentialVerifierFactoryTest, IsCredentialVerifierSupported) {
  EXPECT_TRUE(IsCredentialVerifierSupported(AuthFactorType::kPassword));
  EXPECT_FALSE(IsCredentialVerifierSupported(AuthFactorType::kPin));
  EXPECT_FALSE(
      IsCredentialVerifierSupported(AuthFactorType::kCryptohomeRecovery));
  EXPECT_FALSE(IsCredentialVerifierSupported(AuthFactorType::kKiosk));
  EXPECT_FALSE(IsCredentialVerifierSupported(AuthFactorType::kSmartCard));
  EXPECT_FALSE(IsCredentialVerifierSupported(AuthFactorType::kUnspecified));
}

TEST(CredentialVerifierFactoryTest, CreateCredentialVerifier) {
  const brillo::SecureBlob kPasskey("fake-passkey");
  auto verifier = CreateCredentialVerifier(kPasskey);
  ASSERT_TRUE(verifier);
  EXPECT_TRUE(verifier->Verify(kPasskey));
}

}  // namespace
}  // namespace cryptohome
