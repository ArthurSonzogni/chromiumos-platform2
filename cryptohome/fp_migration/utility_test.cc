// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fp_migration/utility.h"

#include <optional>

#include <gtest/gtest.h>

#include "cryptohome/crypto.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {
namespace {

class FpMigrationUtilityTest : public ::testing::Test {
 protected:
  FakeFeaturesForTesting fake_features_;
  // BiometricsService and Crypto are set to nullptr.
  FpMigrationUtility fp_migration_utility_{
      nullptr, AsyncInitPtr<BiometricsAuthBlockService>(nullptr),
      &fake_features_.async};
};

}  // namespace

// GetLegacyFingerprintMigrationRollout is by default 0.
TEST_F(FpMigrationUtilityTest, GetRolloutDefault) {
  EXPECT_EQ(fp_migration_utility_.GetLegacyFingerprintMigrationRollout(), 0);
}

// Test NeedsMigration's behavior.
TEST_F(FpMigrationUtilityTest, NeedsMigration) {
  // By default, features flags are false. No migraions is needed/allowed.
  EXPECT_FALSE(fp_migration_utility_.NeedsMigration(std::nullopt));
  EXPECT_FALSE(fp_migration_utility_.NeedsMigration(0));
  EXPECT_FALSE(fp_migration_utility_.NeedsMigration(1));

  fake_features_.SetDefaultForFeature(Features::kMigrateLegacyFingerprint,
                                      true);
  EXPECT_TRUE(fp_migration_utility_.NeedsMigration(std::nullopt));
  EXPECT_TRUE(fp_migration_utility_.NeedsMigration(0));
  EXPECT_FALSE(fp_migration_utility_.NeedsMigration(1));
}

}  // namespace cryptohome
