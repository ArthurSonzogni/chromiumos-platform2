// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cryptohome/credential_verifier.h>

#include <memory>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/scrypt_verifier.h"

namespace cryptohome {
namespace {

constexpr char kLabel[] = "fake-label";

class VerifierTest : public ::testing::Test {
 public:
  void SetUp() override {
    password_verifier_ = std::make_unique<ScryptVerifier>(kLabel);
  }

 protected:
  std::unique_ptr<CredentialVerifier> password_verifier_;
};

TEST_F(VerifierTest, AuthFactorType) {
  EXPECT_EQ(password_verifier_->auth_factor_type(), AuthFactorType::kPassword);
}

TEST_F(VerifierTest, AuthFactorLabel) {
  EXPECT_EQ(password_verifier_->auth_factor_label(), kLabel);
}

TEST_F(VerifierTest, Ok) {
  brillo::SecureBlob secret("good");
  EXPECT_TRUE(password_verifier_->Set(secret));
  EXPECT_TRUE(password_verifier_->Verify(secret));
}

TEST_F(VerifierTest, Fail) {
  brillo::SecureBlob secret("good");
  brillo::SecureBlob wrong_secret("wrong");
  EXPECT_TRUE(password_verifier_->Set(secret));
  EXPECT_FALSE(password_verifier_->Verify(wrong_secret));
}

TEST_F(VerifierTest, NotSet) {
  brillo::SecureBlob secret("not set secret");
  EXPECT_FALSE(password_verifier_->Verify(secret));
}

}  // namespace
}  // namespace cryptohome
