// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;

namespace cryptohome {

namespace {

const char kObfuscatedUsername[] = "obfuscated1";
const char kSomeIdpLabel[] = "some-idp";

AuthBlockState CreatePasswordAuthBlockState() {
  TpmBoundToPcrAuthBlockState tpm_bound_to_pcr_auth_block_state = {
      .salt = SecureBlob("fake salt"),
      .tpm_key = SecureBlob("fake tpm key"),
      .extended_tpm_key = SecureBlob("fake extended tpm key"),
      .tpm_public_key_hash = SecureBlob("fake tpm public key hash"),
  };
  AuthBlockState auth_block_state = {.state =
                                         tpm_bound_to_pcr_auth_block_state};
  return auth_block_state;
}

std::unique_ptr<AuthFactor> CreatePasswordAuthFactor() {
  AuthFactorMetadata metadata = {.metadata = PasswordAuthFactorMetadata()};
  return std::make_unique<AuthFactor>(AuthFactorType::kPassword, kSomeIdpLabel,
                                      metadata, CreatePasswordAuthBlockState());
}

}  // namespace

class AuthFactorManagerTest : public ::testing::Test {
 protected:
  MockPlatform platform_;
  AuthFactorManager auth_factor_manager_{&platform_};
};

TEST_F(AuthFactorManagerTest, Save) {
  std::unique_ptr<AuthFactor> auth_factor = CreatePasswordAuthFactor();

  // Persist the auth factor.
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactor(kObfuscatedUsername, *auth_factor));
  EXPECT_TRUE(platform_.FileExists(
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel)));

  // TODO(b/208348570): Test the factor can be loaded back.
}

}  // namespace cryptohome
