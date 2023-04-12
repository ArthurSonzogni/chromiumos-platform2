// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/interface.h"

namespace cryptohome {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;

// Test AuthFactorDriver::NeedsResetSecert. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST(AuthFactorDriverManagerTest, NeedsResetSecret) {
  AuthFactorDriverManager manager;
  auto needs_secret = [&manager](AuthFactorType type) {
    return manager.GetDriver(type).NeedsResetSecret();
  };

  EXPECT_THAT(needs_secret(AuthFactorType::kPassword), IsFalse());
  EXPECT_THAT(needs_secret(AuthFactorType::kPin), IsTrue());
  EXPECT_THAT(needs_secret(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(needs_secret(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(needs_secret(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(needs_secret(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(needs_secret(AuthFactorType::kFingerprint), IsFalse());

  EXPECT_THAT(needs_secret(AuthFactorType::kUnspecified), IsFalse());
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::NeedsRateLimiter. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST(AuthFactorDriverManagerTest, NeedsRateLimiter) {
  AuthFactorDriverManager manager;
  auto needs_limiter = [&manager](AuthFactorType type) {
    return manager.GetDriver(type).NeedsRateLimiter();
  };

  EXPECT_THAT(needs_limiter(AuthFactorType::kPassword), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kPin), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kFingerprint), IsTrue());

  EXPECT_THAT(needs_limiter(AuthFactorType::kUnspecified), IsFalse());

  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::GetAuthFactorLabelArity. We do this here instead of in
// a per-driver test because the check is trivial enough that one test is
// simpler to validate than N separate tests.
TEST(AuthFactorDriverManagerTest, GetAuthFactorLabelArity) {
  AuthFactorDriverManager manager;
  auto get_arity = [&manager](AuthFactorType type) {
    return manager.GetDriver(type).GetAuthFactorLabelArity();
  };

  EXPECT_THAT(get_arity(AuthFactorType::kPassword),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kPin),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kCryptohomeRecovery),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kKiosk),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kSmartCard),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kLegacyFingerprint),
              Eq(AuthFactorLabelArity::kNone));
  EXPECT_THAT(get_arity(AuthFactorType::kFingerprint),
              Eq(AuthFactorLabelArity::kMultiple));

  EXPECT_THAT(get_arity(AuthFactorType::kUnspecified),
              Eq(AuthFactorLabelArity::kNone));
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

}  // namespace
}  // namespace cryptohome
