// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_enforcement_policy.h"

#include <memory>
#include <string>
#include <utility>

#include <base/time/time.h>
#include <base/time/time_override.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <hardware/keymaster_defs.h>
#include <keymaster/authorization_set.h>

#include "absl/strings/escaping.h"

namespace arc::keymint::context {

namespace {

base::Time g_fake_now;
base::Time FakeTimeNow() {
  return g_fake_now;
}

constexpr char kSecretKey[] = "Fake Secret Key";
constexpr char kInputData[] = "I am fake data to be used\n";
constexpr char kExpectedHmacSha256Hex[] =
    "465b0bcb59d8da7b7f9ce006ad21f094e531c06b0a3eb2314715fcda2970c8ed";
}  // namespace

class TestArcEnforcementPolicy : public ArcEnforcementPolicy {
 public:
  using ArcEnforcementPolicy::ArcEnforcementPolicy;

  uint64_t get_current_time_ms() const override { return current_time_ms_; }
  void set_current_time_ms(uint64_t time_ms) { current_time_ms_ = time_ms; }

 private:
  uint64_t current_time_ms_ = 100000;
};

class ArcEnforcementPolicyTest : public ::testing::Test {
 protected:
  ArcEnforcementPolicyTest() {}

  void SetUp() override {
    arc_enforcement_policy_ = new TestArcEnforcementPolicy(64, 64);
  }

  TestArcEnforcementPolicy* arc_enforcement_policy_;
};

class ArcEnforcementPolicyTestPeer {
 public:
  void set_session_key_for_tests(ArcEnforcementPolicy* arc_enforcement_policy,
                                 const std::vector<uint8_t>& session_key) {
    arc_enforcement_policy->set_session_key_for_tests(session_key);
  }
};

TEST_F(ArcEnforcementPolicyTest, ComputeHmacSha256Success) {
  // Prepare
  std::vector<uint8_t> input_data = brillo::BlobFromString(kInputData);
  const std::vector<uint8_t> secret_key = brillo::BlobFromString(kSecretKey);
  auto test_peer = std::make_unique<ArcEnforcementPolicyTestPeer>();
  test_peer->set_session_key_for_tests(arc_enforcement_policy_, secret_key);

  // Execute.
  auto result = arc_enforcement_policy_->ComputeHmac(input_data);

  // Test.
  EXPECT_TRUE(result.isOk());
  EXPECT_EQ(result.error(), KM_ERROR_OK);
  std::vector<uint8_t> result_vector(result.value().begin(),
                                     result.value().end());
  std::string bytes_result = absl::HexStringToBytes(kExpectedHmacSha256Hex);
  std::vector<uint8_t> expected_vector = brillo::BlobFromString(bytes_result);
  EXPECT_EQ(result_vector, expected_vector);
}

TEST_F(ArcEnforcementPolicyTest, ComputeHmacSha256FailureMismatch) {
  // Prepare
  std::vector<uint8_t> input_data = brillo::BlobFromString(kInputData);
  const std::vector<uint8_t> secret_key =
      brillo::BlobFromString("Different Secret Key");
  auto test_peer = std::make_unique<ArcEnforcementPolicyTestPeer>();
  test_peer->set_session_key_for_tests(arc_enforcement_policy_, secret_key);

  // Execute.
  auto result = arc_enforcement_policy_->ComputeHmac(input_data);

  // Test.
  EXPECT_TRUE(result.isOk());
  EXPECT_EQ(result.error(), KM_ERROR_OK);
  std::vector<uint8_t> result_vector(result.value().begin(),
                                     result.value().end());
  std::string bytes_result = absl::HexStringToBytes(kExpectedHmacSha256Hex);
  std::vector<uint8_t> expected_vector = brillo::BlobFromString(bytes_result);
  EXPECT_NE(result_vector, expected_vector);
}

TEST_F(ArcEnforcementPolicyTest, ActivationDateValid) {
  g_fake_now = base::Time::FromMillisecondsSinceUnixEpoch(1700000000000);
  base::subtle::ScopedTimeClockOverrides time_override(&FakeTimeNow, nullptr,
                                                       nullptr);
  uint64_t now_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  uint64_t past_date = now_ms > 10000 ? now_ms - 10000 : 0;
  uint64_t future_date = now_ms + 100000;

  EXPECT_TRUE(arc_enforcement_policy_->activation_date_valid(past_date));
  EXPECT_FALSE(arc_enforcement_policy_->activation_date_valid(future_date));
}

TEST_F(ArcEnforcementPolicyTest, ExpirationDatePassed) {
  g_fake_now = base::Time::FromMillisecondsSinceUnixEpoch(1700000000000);
  base::subtle::ScopedTimeClockOverrides time_override(&FakeTimeNow, nullptr,
                                                       nullptr);
  uint64_t now_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  uint64_t past_date = now_ms > 10000 ? now_ms - 10000 : 0;
  uint64_t future_date = now_ms + 100000;

  EXPECT_TRUE(arc_enforcement_policy_->expiration_date_passed(past_date));
  EXPECT_FALSE(arc_enforcement_policy_->expiration_date_passed(future_date));
}

TEST_F(ArcEnforcementPolicyTest, AuthorizeOperationAllowsNoAuthKey) {
  const ::keymaster::km_id_t kKeyId = 0xdeadbeef;

  ::keymaster::AuthorizationSet hw, sw;
  hw.push_back(::keymaster::TAG_ALGORITHM, KM_ALGORITHM_HMAC);
  hw.push_back(::keymaster::TAG_PURPOSE, KM_PURPOSE_SIGN);
  hw.push_back(::keymaster::TAG_NO_AUTH_REQUIRED);

  ::keymaster::AuthorizationSet op_params;

  keymaster_error_t result = arc_enforcement_policy_->AuthorizeOperation(
      KM_PURPOSE_SIGN, kKeyId, ::keymaster::AuthProxy(hw, sw), op_params,
      /*op_handle=*/0, /*is_begin_operation=*/true);

  EXPECT_EQ(result, KM_ERROR_OK);
}

TEST_F(ArcEnforcementPolicyTest, AuthorizeOperationRejectsInactiveKey) {
  g_fake_now = base::Time::FromMillisecondsSinceUnixEpoch(1700000000000);
  base::subtle::ScopedTimeClockOverrides time_override(&FakeTimeNow, nullptr,
                                                       nullptr);
  const ::keymaster::km_id_t kKeyId = 0xdeadbeef;
  uint64_t now_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  uint64_t future_date = now_ms + 100000;

  ::keymaster::AuthorizationSet hw, sw;
  hw.push_back(::keymaster::TAG_ALGORITHM, KM_ALGORITHM_HMAC);
  hw.push_back(::keymaster::TAG_PURPOSE, KM_PURPOSE_SIGN);
  hw.push_back(::keymaster::TAG_NO_AUTH_REQUIRED);
  hw.push_back(::keymaster::TAG_ACTIVE_DATETIME, future_date);

  ::keymaster::AuthorizationSet op_params;

  keymaster_error_t result = arc_enforcement_policy_->AuthorizeOperation(
      KM_PURPOSE_SIGN, kKeyId, ::keymaster::AuthProxy(hw, sw), op_params,
      /*op_handle=*/0, /*is_begin_operation=*/true);

  EXPECT_EQ(result, KM_ERROR_KEY_NOT_YET_VALID);
}

TEST_F(ArcEnforcementPolicyTest, AuthorizeOperationRejectsExpiredKey) {
  g_fake_now = base::Time::FromMillisecondsSinceUnixEpoch(1700000000000);
  base::subtle::ScopedTimeClockOverrides time_override(&FakeTimeNow, nullptr,
                                                       nullptr);
  const ::keymaster::km_id_t kKeyId = 0xdeadbeef;
  uint64_t now_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  uint64_t past_date = now_ms > 10000 ? now_ms - 10000 : 0;

  ::keymaster::AuthorizationSet hw, sw;
  hw.push_back(::keymaster::TAG_ALGORITHM, KM_ALGORITHM_HMAC);
  hw.push_back(::keymaster::TAG_PURPOSE, KM_PURPOSE_SIGN);
  hw.push_back(::keymaster::TAG_NO_AUTH_REQUIRED);
  hw.push_back(::keymaster::TAG_ORIGINATION_EXPIRE_DATETIME, past_date);

  ::keymaster::AuthorizationSet op_params;

  keymaster_error_t result = arc_enforcement_policy_->AuthorizeOperation(
      KM_PURPOSE_SIGN, kKeyId, ::keymaster::AuthProxy(hw, sw), op_params,
      /*op_handle=*/0, /*is_begin_operation=*/true);

  EXPECT_EQ(result, KM_ERROR_KEY_EXPIRED);
}

TEST_F(ArcEnforcementPolicyTest, AuthTokenTimedOut_FreshToken) {
  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  hw_auth_token_t token{};
  token.timestamp = ::keymaster::hton(now_ms);

  EXPECT_FALSE(arc_enforcement_policy_->auth_token_timed_out(token, 30));
}

TEST_F(ArcEnforcementPolicyTest, AuthTokenTimedOut_ExpiredToken) {
  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  // 31 seconds ago (timeout is 30 seconds)
  uint64_t past_ms = now_ms - 31000;
  hw_auth_token_t token{};
  token.timestamp = ::keymaster::hton(past_ms);

  EXPECT_TRUE(arc_enforcement_policy_->auth_token_timed_out(token, 30));
}

TEST_F(ArcEnforcementPolicyTest, AuthTokenTimedOut_FutureTokenWithinTolerance) {
  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  // 1 second ahead in the future (within 2-second tolerance window)
  uint64_t future_ms = now_ms + 1000;
  hw_auth_token_t token{};
  token.timestamp = ::keymaster::hton(future_ms);

  EXPECT_FALSE(arc_enforcement_policy_->auth_token_timed_out(token, 30));
}

TEST_F(ArcEnforcementPolicyTest, AuthTokenTimedOut_FutureTokenBeyondTolerance) {
  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  // 10 seconds ahead in the future (exceeds 2-second tolerance window)
  uint64_t future_ms = now_ms + 10000;
  hw_auth_token_t token{};
  token.timestamp = ::keymaster::hton(future_ms);

  EXPECT_TRUE(arc_enforcement_policy_->auth_token_timed_out(token, 30));
}

TEST_F(ArcEnforcementPolicyTest, AuthTokenTimedOut_PerOperationZeroTimeout) {
  hw_auth_token_t token{};
  token.timestamp = ::keymaster::hton(0ULL);

  EXPECT_FALSE(arc_enforcement_policy_->auth_token_timed_out(token, 0));
}

}  // namespace arc::keymint::context
