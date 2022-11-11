// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor_map.h"

#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_factor/auth_factor.h"

namespace cryptohome {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;

constexpr char kLabel1[] = "factor1";
constexpr char kLabel2[] = "factor2";

// Make a password auth factor with the given label. There metadata and state
// are empty because for testing the map we just need any factor with a label.
// Returns a "unique_ptr, ptr" pair so that tests that we can hand off ownership
// to the map while holding on to a raw pointer.
std::pair<std::unique_ptr<AuthFactor>, AuthFactor*> MakeFactor(
    std::string label) {
  auto owned_ptr =
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, std::move(label),
                                   AuthFactorMetadata(), AuthBlockState());
  auto* unowned_ptr = owned_ptr.get();
  return {std::move(owned_ptr), unowned_ptr};
}

class AuthFactorMapTest : public testing::Test {
 protected:
  // Returns a const-ref to the test object. Used for testing const methods.
  const AuthFactorMap& const_factor_map() const { return factor_map_; }

  AuthFactorMap factor_map_;
};

TEST_F(AuthFactorMapTest, InitialEmpty) {
  EXPECT_THAT(factor_map_.empty(), IsTrue());
  EXPECT_THAT(factor_map_.size(), Eq(0));
  EXPECT_THAT(factor_map_.Find(kLabel1), IsNull());
  EXPECT_THAT(factor_map_.Find(kLabel2), IsNull());
  EXPECT_THAT(const_factor_map().Find(kLabel1), IsNull());
  EXPECT_THAT(const_factor_map().Find(kLabel2), IsNull());
}

TEST_F(AuthFactorMapTest, AddOne) {
  auto [factor, ptr] = MakeFactor(kLabel1);
  factor_map_.Add(std::move(factor));

  EXPECT_THAT(factor_map_.empty(), IsFalse());
  EXPECT_THAT(factor_map_.size(), Eq(1));
  EXPECT_THAT(factor_map_.Find(kLabel1), Eq(ptr));
  EXPECT_THAT(factor_map_.Find(kLabel2), IsNull());
  EXPECT_THAT(const_factor_map().Find(kLabel1), Eq(ptr));
  EXPECT_THAT(const_factor_map().Find(kLabel2), IsNull());
}

TEST_F(AuthFactorMapTest, AddTwo) {
  auto [factor1, ptr1] = MakeFactor(kLabel1);
  auto [factor2, ptr2] = MakeFactor(kLabel2);
  factor_map_.Add(std::move(factor1));
  factor_map_.Add(std::move(factor2));

  EXPECT_THAT(factor_map_.empty(), IsFalse());
  EXPECT_THAT(factor_map_.size(), Eq(2));
  EXPECT_THAT(factor_map_.Find(kLabel1), Eq(ptr1));
  EXPECT_THAT(factor_map_.Find(kLabel2), Eq(ptr2));
  EXPECT_THAT(const_factor_map().Find(kLabel1), Eq(ptr1));
  EXPECT_THAT(const_factor_map().Find(kLabel2), Eq(ptr2));
}

TEST_F(AuthFactorMapTest, AddDuplicate) {
  auto [factor1, ptr1] = MakeFactor(kLabel1);
  auto [factor2, ptr2] = MakeFactor(kLabel1);
  factor_map_.Add(std::move(factor1));
  factor_map_.Add(std::move(factor2));

  EXPECT_THAT(factor_map_.empty(), IsFalse());
  EXPECT_THAT(factor_map_.size(), Eq(1));
  EXPECT_THAT(factor_map_.Find(kLabel1), Eq(ptr2));
  EXPECT_THAT(factor_map_.Find(kLabel2), IsNull());
  EXPECT_THAT(const_factor_map().Find(kLabel1), Eq(ptr2));
  EXPECT_THAT(const_factor_map().Find(kLabel2), IsNull());
}

}  // namespace
}  // namespace cryptohome
