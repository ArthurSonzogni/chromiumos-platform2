// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/credential_verifier.h"
#include "cryptohome/credential_verifier_factory.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {
namespace {

constexpr char kLabel[] = "fake-label";

AuthInput MakePasswordInput() {
  return {.user_input = brillo::SecureBlob("fake-passkey")};
}

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
  AuthInput auth_input = MakePasswordInput();
  auto verifier =
      CreateCredentialVerifier(AuthFactorType::kPassword, kLabel, auth_input);
  ASSERT_TRUE(verifier);
  EXPECT_EQ(verifier->auth_factor_type(), AuthFactorType::kPassword);
  EXPECT_TRUE(verifier->Verify(*auth_input.user_input));
}

TEST(CredentialVerifierFactoryTest, CreateCredentialVerifierUnsupported) {
  AuthInput auth_input = MakePasswordInput();
  EXPECT_EQ(CreateCredentialVerifier(AuthFactorType::kPin, kLabel, auth_input),
            nullptr);
  EXPECT_EQ(CreateCredentialVerifier(AuthFactorType::kCryptohomeRecovery,
                                     kLabel, auth_input),
            nullptr);
  EXPECT_EQ(
      CreateCredentialVerifier(AuthFactorType::kKiosk, kLabel, auth_input),
      nullptr);
  EXPECT_EQ(
      CreateCredentialVerifier(AuthFactorType::kSmartCard, kLabel, auth_input),
      nullptr);
  EXPECT_EQ(CreateCredentialVerifier(AuthFactorType::kUnspecified, kLabel,
                                     auth_input),
            nullptr);
}

}  // namespace
}  // namespace cryptohome
