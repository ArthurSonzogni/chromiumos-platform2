// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <absl/types/variant.h>
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
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Pair;

namespace cryptohome {

namespace {

const char kObfuscatedUsername[] = "obfuscated1";
const char kSomeIdpLabel[] = "some-idp";

AuthBlockState CreatePasswordAuthBlockState() {
  TpmBoundToPcrAuthBlockState tpm_bound_to_pcr_auth_block_state = {
      .scrypt_derived = false,
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

// Test the `SaveAuthFactor()` method correctly serializes the factor into a
// file.
TEST_F(AuthFactorManagerTest, Save) {
  std::unique_ptr<AuthFactor> auth_factor = CreatePasswordAuthFactor();

  // Persist the auth factor.
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactor(kObfuscatedUsername, *auth_factor));
  EXPECT_TRUE(platform_.FileExists(
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel)));

  // Load the auth factor and verify it's the same.
  std::unique_ptr<AuthFactor> loaded_auth_factor =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  ASSERT_TRUE(loaded_auth_factor);
  EXPECT_EQ(loaded_auth_factor->type(), AuthFactorType::kPassword);
  EXPECT_EQ(loaded_auth_factor->label(), kSomeIdpLabel);
  EXPECT_TRUE(absl::holds_alternative<PasswordAuthFactorMetadata>(
      loaded_auth_factor->metadata().metadata));
  // TODO(b/204441443): Check other fields too. Consider using a GTest matcher.
}

// Test the `SaveAuthFactor()` method fails when the label is empty.
TEST_F(AuthFactorManagerTest, SaveBadEmptyLabel) {
  // Create an auth factor as a clone of a correct object, but with an empty
  // label.
  std::unique_ptr<AuthFactor> good_auth_factor = CreatePasswordAuthFactor();
  AuthFactor bad_auth_factor(good_auth_factor->type(),
                             /*label=*/std::string(),
                             good_auth_factor->metadata(),
                             good_auth_factor->auth_block_state());

  // Verify the manager refuses to save this auth factor.
  EXPECT_FALSE(auth_factor_manager_.SaveAuthFactor(kObfuscatedUsername,
                                                   bad_auth_factor));
}

// Test the `SaveAuthFactor()` method fails when the label contains forbidden
// characters.
TEST_F(AuthFactorManagerTest, SaveBadMalformedLabel) {
  // Create an auth factor as a clone of a correct object, but with a malformed
  // label.
  std::unique_ptr<AuthFactor> good_auth_factor = CreatePasswordAuthFactor();
  AuthFactor bad_auth_factor(good_auth_factor->type(),
                             /*label=*/"foo.' bar'",
                             good_auth_factor->metadata(),
                             good_auth_factor->auth_block_state());

  // Verify the manager refuses to save this auth factor.
  EXPECT_FALSE(auth_factor_manager_.SaveAuthFactor(kObfuscatedUsername,
                                                   bad_auth_factor));
}

// Test that `ListAuthFactors()` returns an empty map when there's no auth
// factor added.
TEST_F(AuthFactorManagerTest, ListEmpty) {
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map, IsEmpty());
}

// Test that `ListAuthFactors()` returns the auth factor that was added.
TEST_F(AuthFactorManagerTest, ListSingle) {
  // Create the auth factor file.
  std::unique_ptr<AuthFactor> auth_factor = CreatePasswordAuthFactor();
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactor(kObfuscatedUsername, *auth_factor));

  // Verify the factor is listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor without a file name
// extension (and hence without a label).
TEST_F(AuthFactorManagerTest, ListBadNoExtension) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("password"), /*blob=*/{});

  // Verify the malformed file is ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor with an empty file name
// extension (and hence without a label).
TEST_F(AuthFactorManagerTest, ListBadEmptyExtension) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("password."), /*blob=*/{});

  // Verify the malformed file is ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor with multiple file name
// extensions (and hence with an incorrect label).
TEST_F(AuthFactorManagerTest, ListBadMultipleExtensions) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("password.label.garbage"),
      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("password.tar.gz"),
      /*blob=*/{});

  // Verify the malformed files are ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor with the file name
// consisting of just an extension (and hence without a factor type).
TEST_F(AuthFactorManagerTest, ListBadEmptyType) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername).Append(".label"),
                      /*blob=*/{});

  // Verify the malformed file is ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor whose file name has a
// garbage instead of the factor type.
TEST_F(AuthFactorManagerTest, ListBadUnknownType) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("fancytype.label"),
      /*blob=*/{});

  // Verify the malformed file is ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// TODO(b:208348570): Test clash of labels once more than one factor type is
// supported by AuthFactorManager.

}  // namespace cryptohome
