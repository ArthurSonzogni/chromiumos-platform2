// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cryptohome/credential_verifier.h>

#include <memory>
#include <variant>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {
namespace {

// Minimal concrete implementation of CredentialVerifier, so that we can test
// the abstract base class functions.
class TestVerifier : public CredentialVerifier {
 public:
  TestVerifier(AuthFactorType auth_factor_type,
               std::string auth_factor_label,
               AuthFactorMetadata auth_factor_metadata)
      : CredentialVerifier(auth_factor_type,
                           std::move(auth_factor_label),
                           std::move(auth_factor_metadata)) {}

  bool Verify(const brillo::SecureBlob& secret) const override { return false; }
};

class CredentialVerifierTest : public ::testing::Test {
 public:
  CredentialVerifierTest()
      : pw_verifier_(AuthFactorType::kPassword,
                     "password",
                     {.metadata = PasswordAuthFactorMetadata()}),
        pin_verifier_(AuthFactorType::kPin,
                      "pin",
                      {.metadata = PinAuthFactorMetadata()}) {}

 protected:
  // A couple of verifiers that we can test with.
  TestVerifier pw_verifier_;
  TestVerifier pin_verifier_;
};

TEST_F(CredentialVerifierTest, AuthFactorType) {
  EXPECT_EQ(pw_verifier_.auth_factor_type(), AuthFactorType::kPassword);
  EXPECT_EQ(pin_verifier_.auth_factor_type(), AuthFactorType::kPin);
}

TEST_F(CredentialVerifierTest, AuthFactorLabel) {
  EXPECT_EQ(pw_verifier_.auth_factor_label(), "password");
  EXPECT_EQ(pin_verifier_.auth_factor_label(), "pin");
}

TEST_F(CredentialVerifierTest, AuthFactorMetadata) {
  EXPECT_TRUE(std::holds_alternative<PasswordAuthFactorMetadata>(
      pw_verifier_.auth_factor_metadata().metadata));
  EXPECT_TRUE(std::holds_alternative<PinAuthFactorMetadata>(
      pin_verifier_.auth_factor_metadata().metadata));
}

}  // namespace
}  // namespace cryptohome
