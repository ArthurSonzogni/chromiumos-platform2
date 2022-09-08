// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

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

TEST(CredentialVerifierFactoryTest, CreateCredentialVerifierPassword) {
  const brillo::SecureBlob kPasskey("fake-passkey");
  auto verifier = CreateCredentialVerifier(AuthFactorType::kPassword, kPasskey);
  ASSERT_TRUE(verifier);
  EXPECT_EQ(verifier->auth_factor_type(), AuthFactorType::kPassword);
  EXPECT_TRUE(verifier->Verify(kPasskey));
}

TEST(CredentialVerifierFactoryTest, CreateCredentialVerifierUntyped) {
  const brillo::SecureBlob kPasskey("fake-passkey");
  auto verifier =
      CreateCredentialVerifier(/*auth_factor_type=*/std::nullopt, kPasskey);
  ASSERT_TRUE(verifier);
  EXPECT_TRUE(verifier->Verify(kPasskey));
}

TEST(CredentialVerifierFactoryTest, CreateCredentialVerifierUnsupported) {
  const brillo::SecureBlob kPasskey("fake-passkey");
  EXPECT_EQ(CreateCredentialVerifier(AuthFactorType::kPin, kPasskey), nullptr);
  EXPECT_EQ(
      CreateCredentialVerifier(AuthFactorType::kCryptohomeRecovery, kPasskey),
      nullptr);
  EXPECT_EQ(CreateCredentialVerifier(AuthFactorType::kKiosk, kPasskey),
            nullptr);
  EXPECT_EQ(CreateCredentialVerifier(AuthFactorType::kSmartCard, kPasskey),
            nullptr);
  EXPECT_EQ(CreateCredentialVerifier(AuthFactorType::kUnspecified, kPasskey),
            nullptr);
}

}  // namespace
}  // namespace cryptohome
