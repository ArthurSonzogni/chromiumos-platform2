// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/dcheck_is_on.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "featured/hmac.h"

namespace {
using ::testing::SizeIs;
}  // namespace

namespace featured {

// Check that basic verification works as expected.
TEST(HMAC, SignAndVerify) {
  HMAC hmacer(HMAC::SHA256);
  ASSERT_TRUE(hmacer.Init());
  std::optional<std::string> hmac_maybe = hmacer.Sign("data");
  ASSERT_TRUE(hmac_maybe.has_value());
  std::string hmac = hmac_maybe.value();

  EXPECT_THAT(hmac, SizeIs(32));  // SHA256 should give a 32-byte digest.

  // Same data should verify (even if not identical by pointer)
  char other_data[] = "Aata";  // force compiler to give a different address.
  other_data[0] = 'd';
  EXPECT_TRUE(hmacer.Verify(other_data, hmac));

  // Different data should *not* verify.
  EXPECT_FALSE(hmacer.Verify("not data", hmac));

  // Should generate a different key and thus not verify.
  HMAC hmacer_2(HMAC::SHA256);
  ASSERT_TRUE(hmacer_2.Init());
  EXPECT_FALSE(hmacer_2.Verify("data", hmac));
}

// HMAC should only verify if length matches.
TEST(HMAC, VerifyBadSize) {
  HMAC hmacer(HMAC::SHA256);
  ASSERT_TRUE(hmacer.Init());
  std::optional<std::string> hmac_maybe = hmacer.Sign("data");
  ASSERT_TRUE(hmac_maybe.has_value());
  std::string hmac = hmac_maybe.value();

  std::string hmac_long = hmac + "2";
  EXPECT_FALSE(hmacer.Verify("data", hmac_long));

  std::string hmac_short = hmac.substr(0, hmac.size() - 1);
  ASSERT_EQ(hmac_short.size(), hmac.size() - 1);
  EXPECT_FALSE(hmacer.Verify("data", hmac_short));
}

// Check that specifying a constant test key produces consistent results.
TEST(HMAC, SignAndVerify_FakeKey) {
  HMAC hmacer(HMAC::SHA256);
  ASSERT_TRUE(hmacer.Init("fakekey"));
  std::optional<std::string> hmac_maybe = hmacer.Sign("data");
  ASSERT_TRUE(hmac_maybe.has_value());
  std::string hmac = hmac_maybe.value();

  // Key should be as set.
  EXPECT_EQ(hmacer.GetKey(), "fakekey");

  // Different data should *not* verify.
  EXPECT_FALSE(hmacer.Verify("not data", hmac));

  // Should use the same key and thus verify.
  HMAC hmacer_2(HMAC::SHA256);
  ASSERT_TRUE(hmacer_2.Init("fakekey"));
  EXPECT_TRUE(hmacer_2.Verify("data", hmac));
}

// Test that Init() generates an appropriate-length key, and that it is
// not all 0.
TEST(HMAC, Init) {
  std::string bad_key(32, '\0');
  ASSERT_THAT(bad_key, SizeIs(32));

  HMAC hmacer(HMAC::SHA256);
  ASSERT_TRUE(hmacer.Init());
  std::string actual = hmacer.GetKey();
  EXPECT_THAT(actual, SizeIs(32));
  EXPECT_NE(actual, bad_key);
}

// Check that we can create a class using a key with null bytes and other
// non-ASCII bytes.
TEST(HMAC, FakeKey_ArbitraryBytes) {
  std::string key("\xff\x00\xca\xfe", 4);
  HMAC hmacer(HMAC::SHA256);
  ASSERT_TRUE(hmacer.Init(key));
  // Key should be as set.
  EXPECT_THAT(hmacer.GetKey(), SizeIs(4));
  EXPECT_EQ(hmacer.GetKey(), key);
}

// If Init is called twice, the new key should be different.
TEST(HMAC, GenerateTwice) {
  HMAC hmacer(HMAC::SHA256);
  ASSERT_TRUE(hmacer.Init());
  std::string k1 = hmacer.GetKey();
  ASSERT_TRUE(hmacer.Init());
  EXPECT_NE(hmacer.GetKey(), k1);
}

#if DCHECK_IS_ON()
// Verify that sign DCHECKs if key isn't initialized.
TEST(HMACDeath, SignDieEmptyKey) {
  HMAC hmacer(HMAC::SHA256);
  EXPECT_DEATH(hmacer.Sign("data"), "Class not initialized");
}

// Verify that verify DCHECKs if key isn't initialized.
TEST(HMACDeath, VerifyDieEmptyKey) {
  HMAC hmacer(HMAC::SHA256);
  EXPECT_DEATH(hmacer.Verify("data", "hmac"), "Class not initialized");
}
#else   // DCHECK_IS_ON()
// Verify that sign returns nullopt if DCHECK is off and key is empty.
TEST(HMAC, SignFailEmptyKey) {
  HMAC hmacer(HMAC::SHA256);
  EXPECT_EQ(hmacer.Sign("data"), std::nullopt);
}

// Verify that verification fails if DCHECK is off and key is empty.
TEST(HMAC, VerifyFailEmptyKey) {
  HMAC hmacer(HMAC::SHA256);
  EXPECT_FALSE(hmacer.Verify("data", "hmac"));
}
#endif  // DCHECK_IS_ON()

}  // namespace featured
