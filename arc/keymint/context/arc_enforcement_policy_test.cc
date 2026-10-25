// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_enforcement_policy.h"

#include <memory>
#include <string>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <base/time/time_override.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <hardware/hw_auth_token.h>
#include <keymaster/android_keymaster_utils.h>
#include <keymaster/authorization_set.h>
#include <keymaster/cppcose/cppcose.h>

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

  void set_shared_hmac_key_for_tests(
      ArcEnforcementPolicy* arc_enforcement_policy,
      const std::vector<uint8_t>& shared_hmac_key) {
    arc_enforcement_policy->set_shared_hmac_key_for_tests(shared_hmac_key);
  }

  const std::vector<uint8_t>& get_shared_hmac_key_for_tests(
      const ArcEnforcementPolicy* arc_enforcement_policy) {
    return arc_enforcement_policy->shared_hmac_key_;
  }
};

namespace {

hw_auth_token_t MintAuthToken(const std::vector<uint8_t>& key,
                              uint64_t challenge,
                              uint64_t user_id,
                              uint64_t authenticator_id,
                              hw_authenticator_type_t auth_type,
                              uint64_t timestamp_ms) {
  hw_auth_token_t token{};
  token.version = HW_AUTH_TOKEN_VERSION;
  token.challenge = challenge;
  token.user_id = user_id;
  token.authenticator_id = authenticator_id;
  token.authenticator_type =
      ::keymaster::hton(static_cast<uint32_t>(auth_type));
  token.timestamp = ::keymaster::hton(timestamp_ms);

  constexpr size_t kHashableLength = offsetof(hw_auth_token_t, hmac);
  std::vector<uint8_t> data_to_mac(
      reinterpret_cast<const uint8_t*>(&token),
      reinterpret_cast<const uint8_t*>(&token) + kHashableLength);
  auto hmac_result = cppcose::generateHmacSha256(key, data_to_mac);
  EXPECT_TRUE(static_cast<bool>(hmac_result));
  if (hmac_result) {
    memcpy(token.hmac, hmac_result->data(), sizeof(token.hmac));
  }
  return token;
}

}  // namespace

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
  std::string bytes_result;
  base::HexStringToString(kExpectedHmacSha256Hex, &bytes_result);
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
  std::string bytes_result;
  base::HexStringToString(kExpectedHmacSha256Hex, &bytes_result);
  std::vector<uint8_t> expected_vector = brillo::BlobFromString(bytes_result);
  EXPECT_NE(result_vector, expected_vector);
}

TEST_F(ArcEnforcementPolicyTest, ActivationDateValid) {
  g_fake_now = base::Time::FromMillisecondsSinceUnixEpoch(1700000000000);
  base::subtle::ScopedTimeClockOverrides time_override(&FakeTimeNow, nullptr,
                                                       nullptr);
  uint64_t now_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  uint64_t past_date = now_ms - 10000;
  uint64_t future_date = now_ms + 100000;

  EXPECT_TRUE(arc_enforcement_policy_->activation_date_valid(past_date));
  EXPECT_FALSE(arc_enforcement_policy_->activation_date_valid(future_date));
}

TEST_F(ArcEnforcementPolicyTest, ExpirationDatePassed) {
  g_fake_now = base::Time::FromMillisecondsSinceUnixEpoch(1700000000000);
  base::subtle::ScopedTimeClockOverrides time_override(&FakeTimeNow, nullptr,
                                                       nullptr);
  uint64_t now_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  uint64_t past_date = now_ms - 10000;
  uint64_t future_date = now_ms + 100000;

  EXPECT_TRUE(arc_enforcement_policy_->expiration_date_passed(past_date));
  EXPECT_FALSE(arc_enforcement_policy_->expiration_date_passed(future_date));
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

TEST_F(ArcEnforcementPolicyTest, ValidateTokenSignature_NoSharedKey) {
  hw_auth_token_t token{};
  EXPECT_FALSE(arc_enforcement_policy_->ValidateTokenSignature(token));
}

TEST_F(ArcEnforcementPolicyTest, ValidateTokenSignature_ValidSignature) {
  const std::vector<uint8_t> hmac_key(32, 0x42);
  auto test_peer = std::make_unique<ArcEnforcementPolicyTestPeer>();
  test_peer->set_shared_hmac_key_for_tests(arc_enforcement_policy_, hmac_key);

  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  hw_auth_token_t token =
      MintAuthToken(hmac_key, /*challenge=*/0, /*user_id=*/1000,
                    /*authenticator_id=*/2000, HW_AUTH_FINGERPRINT, now_ms);

  EXPECT_TRUE(arc_enforcement_policy_->ValidateTokenSignature(token));
}

TEST_F(ArcEnforcementPolicyTest, ValidateTokenSignature_ForgedSignature) {
  const std::vector<uint8_t> hmac_key(32, 0x42);
  auto test_peer = std::make_unique<ArcEnforcementPolicyTestPeer>();
  test_peer->set_shared_hmac_key_for_tests(arc_enforcement_policy_, hmac_key);

  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  hw_auth_token_t token =
      MintAuthToken(hmac_key, /*challenge=*/0, /*user_id=*/1000,
                    /*authenticator_id=*/2000, HW_AUTH_FINGERPRINT, now_ms);

  // Corrupt the signature.
  token.hmac[0] ^= 0xff;

  EXPECT_FALSE(arc_enforcement_policy_->ValidateTokenSignature(token));
}

TEST_F(ArcEnforcementPolicyTest, ComputeSharedHmac_Success) {
  // Ensure that initially no shared HMAC key exists and token validation fails
  // closed.
  auto test_peer = std::make_unique<ArcEnforcementPolicyTestPeer>();
  EXPECT_TRUE(test_peer->get_shared_hmac_key_for_tests(arc_enforcement_policy_)
                  .empty());

  ::keymaster::HmacSharingParameters my_params;
  ASSERT_EQ(arc_enforcement_policy_->GetHmacSharingParameters(&my_params),
            KM_ERROR_OK);

  ::keymaster::HmacSharingParametersArray params_array;
  params_array.params_array =
      new (std::nothrow)::keymaster::HmacSharingParameters[2];
  ASSERT_NE(params_array.params_array, nullptr);
  params_array.num_params = 2;

  params_array.params_array[0].seed = std::move(my_params.seed);
  memcpy(params_array.params_array[0].nonce, my_params.nonce,
         sizeof(my_params.nonce));

  params_array.params_array[1].seed = {};
  memset(params_array.params_array[1].nonce, 0x42,
         sizeof(params_array.params_array[1].nonce));

  ::keymaster::KeymasterBlob sharing_check;
  ASSERT_EQ(
      arc_enforcement_policy_->ComputeSharedHmac(params_array, &sharing_check),
      KM_ERROR_OK);
  EXPECT_EQ(sharing_check.data_length, 32u);

  const std::vector<uint8_t>& derived_key =
      test_peer->get_shared_hmac_key_for_tests(arc_enforcement_policy_);
  EXPECT_EQ(derived_key.size(), 32u);

  // Verify that the CKDF-derived key matches the key used by
  // SoftKeymasterEnforcement::ComputeSharedHmac to compute sharing_check.
  auto expected_check = cppcose::generateHmacSha256(
      derived_key, brillo::BlobFromString("Keymaster HMAC Verification"));
  ASSERT_TRUE(static_cast<bool>(expected_check));
  EXPECT_EQ(
      std::vector<uint8_t>(sharing_check.data,
                           sharing_check.data + sharing_check.data_length),
      std::vector<uint8_t>(expected_check->begin(), expected_check->end()));

  // Validate that tokens signed with the CKDF-derived key are accepted.
  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  hw_auth_token_t valid_token =
      MintAuthToken(derived_key, /*challenge=*/12345, /*user_id=*/1000,
                    /*authenticator_id=*/2000, HW_AUTH_FINGERPRINT, now_ms);
  EXPECT_TRUE(arc_enforcement_policy_->ValidateTokenSignature(valid_token));

  // Validate that tokens with a corrupted signature are rejected.
  valid_token.hmac[0] ^= 0xff;
  EXPECT_FALSE(arc_enforcement_policy_->ValidateTokenSignature(valid_token));
}

TEST_F(ArcEnforcementPolicyTest, ComputeSharedHmac_MissingSelfParams) {
  ::keymaster::HmacSharingParameters my_params;
  ASSERT_EQ(arc_enforcement_policy_->GetHmacSharingParameters(&my_params),
            KM_ERROR_OK);

  ::keymaster::HmacSharingParametersArray params_array;
  params_array.params_array =
      new (std::nothrow)::keymaster::HmacSharingParameters[1];
  ASSERT_NE(params_array.params_array, nullptr);
  params_array.num_params = 1;

  params_array.params_array[0].seed = {};
  memset(params_array.params_array[0].nonce, 0x11,
         sizeof(params_array.params_array[0].nonce));

  ::keymaster::KeymasterBlob sharing_check;
  EXPECT_EQ(
      arc_enforcement_policy_->ComputeSharedHmac(params_array, &sharing_check),
      KM_ERROR_INVALID_ARGUMENT);
}

TEST_F(ArcEnforcementPolicyTest, AuthorizeOperationAllowsValidAuthToken) {
  const uint64_t kVictimSid = 0x1122334455667788ULL;
  const ::keymaster::km_id_t kKeyId = 0xdeadbeef;
  const std::vector<uint8_t> hmac_key(32, 0x55);

  auto test_peer = std::make_unique<ArcEnforcementPolicyTestPeer>();
  test_peer->set_shared_hmac_key_for_tests(arc_enforcement_policy_, hmac_key);

  ::keymaster::AuthorizationSet hw, sw;
  hw.push_back(::keymaster::TAG_ALGORITHM, KM_ALGORITHM_HMAC);
  hw.push_back(::keymaster::TAG_PURPOSE, KM_PURPOSE_SIGN);
  hw.push_back(::keymaster::TAG_USER_SECURE_ID, kVictimSid);
  hw.push_back(::keymaster::TAG_USER_AUTH_TYPE, HW_AUTH_FINGERPRINT);
  hw.push_back(::keymaster::TAG_AUTH_TIMEOUT, 30u);

  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  hw_auth_token_t tok =
      MintAuthToken(hmac_key, /*challenge=*/0, kVictimSid,
                    /*authenticator_id=*/0, HW_AUTH_FINGERPRINT, now_ms);

  ::keymaster::AuthorizationSet op_params;
  op_params.push_back(::keymaster::TAG_AUTH_TOKEN,
                      reinterpret_cast<const uint8_t*>(&tok), sizeof(tok));

  keymaster_error_t result = arc_enforcement_policy_->AuthorizeOperation(
      KM_PURPOSE_SIGN, kKeyId, ::keymaster::AuthProxy(hw, sw), op_params,
      /*op_handle=*/0, /*is_begin_operation=*/true);

  EXPECT_EQ(result, KM_ERROR_OK);
}

TEST_F(ArcEnforcementPolicyTest, AuthorizeOperationRejectsForgedAuthToken) {
  const uint64_t kVictimSid = 0x1122334455667788ULL;
  const ::keymaster::km_id_t kKeyId = 0xdeadbeef;
  const std::vector<uint8_t> hmac_key(32, 0x55);

  auto test_peer = std::make_unique<ArcEnforcementPolicyTestPeer>();
  test_peer->set_shared_hmac_key_for_tests(arc_enforcement_policy_, hmac_key);

  ::keymaster::AuthorizationSet hw, sw;
  hw.push_back(::keymaster::TAG_ALGORITHM, KM_ALGORITHM_HMAC);
  hw.push_back(::keymaster::TAG_PURPOSE, KM_PURPOSE_SIGN);
  hw.push_back(::keymaster::TAG_USER_SECURE_ID, kVictimSid);
  hw.push_back(::keymaster::TAG_USER_AUTH_TYPE, HW_AUTH_FINGERPRINT);
  hw.push_back(::keymaster::TAG_AUTH_TIMEOUT, 30u);

  hw_auth_token_t tok{};
  tok.version = HW_AUTH_TOKEN_VERSION;
  tok.user_id = kVictimSid;
  tok.authenticator_type =
      ::keymaster::hton(static_cast<uint32_t>(HW_AUTH_FINGERPRINT));
  memset(tok.hmac, 0x41, sizeof(tok.hmac));

  ::keymaster::AuthorizationSet op_params;
  op_params.push_back(::keymaster::TAG_AUTH_TOKEN,
                      reinterpret_cast<const uint8_t*>(&tok), sizeof(tok));

  keymaster_error_t result = arc_enforcement_policy_->AuthorizeOperation(
      KM_PURPOSE_SIGN, kKeyId, ::keymaster::AuthProxy(hw, sw), op_params,
      /*op_handle=*/0, /*is_begin_operation=*/true);

  EXPECT_EQ(result, KM_ERROR_KEY_USER_NOT_AUTHENTICATED);
}

TEST_F(ArcEnforcementPolicyTest, AuthorizeOperationRejectsExpiredAuthToken) {
  const uint64_t kVictimSid = 0x1122334455667788ULL;
  const ::keymaster::km_id_t kKeyId = 0xdeadbeef;
  const std::vector<uint8_t> hmac_key(32, 0x55);

  auto test_peer = std::make_unique<ArcEnforcementPolicyTestPeer>();
  test_peer->set_shared_hmac_key_for_tests(arc_enforcement_policy_, hmac_key);

  ::keymaster::AuthorizationSet hw, sw;
  hw.push_back(::keymaster::TAG_ALGORITHM, KM_ALGORITHM_HMAC);
  hw.push_back(::keymaster::TAG_PURPOSE, KM_PURPOSE_SIGN);
  hw.push_back(::keymaster::TAG_USER_SECURE_ID, kVictimSid);
  hw.push_back(::keymaster::TAG_USER_AUTH_TYPE, HW_AUTH_FINGERPRINT);
  hw.push_back(::keymaster::TAG_AUTH_TIMEOUT, 30u);

  uint64_t now_ms = arc_enforcement_policy_->get_current_time_ms();
  uint64_t past_ms = now_ms - 35000;
  hw_auth_token_t tok =
      MintAuthToken(hmac_key, /*challenge=*/0, kVictimSid,
                    /*authenticator_id=*/0, HW_AUTH_FINGERPRINT, past_ms);

  ::keymaster::AuthorizationSet op_params;
  op_params.push_back(::keymaster::TAG_AUTH_TOKEN,
                      reinterpret_cast<const uint8_t*>(&tok), sizeof(tok));

  keymaster_error_t result = arc_enforcement_policy_->AuthorizeOperation(
      KM_PURPOSE_SIGN, kKeyId, ::keymaster::AuthProxy(hw, sw), op_params,
      /*op_handle=*/0, /*is_begin_operation=*/true);

  EXPECT_EQ(result, KM_ERROR_KEY_USER_NOT_AUTHENTICATED);
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
  const ::keymaster::km_id_t kKeyId = 0xdeadbeef;

  ::keymaster::AuthorizationSet hw, sw;
  hw.push_back(::keymaster::TAG_ALGORITHM, KM_ALGORITHM_HMAC);
  hw.push_back(::keymaster::TAG_PURPOSE, KM_PURPOSE_SIGN);
  hw.push_back(::keymaster::TAG_NO_AUTH_REQUIRED);
  uint64_t future_date = static_cast<uint64_t>(
      (base::Time::Now() + base::Days(30)).InMillisecondsSinceUnixEpoch());
  hw.push_back(::keymaster::TAG_ACTIVE_DATETIME, future_date);

  ::keymaster::AuthorizationSet op_params;
  keymaster_error_t result = arc_enforcement_policy_->AuthorizeOperation(
      KM_PURPOSE_SIGN, kKeyId, ::keymaster::AuthProxy(hw, sw), op_params,
      /*op_handle=*/0, /*is_begin_operation=*/true);

  EXPECT_EQ(result, KM_ERROR_KEY_NOT_YET_VALID);
}

TEST_F(ArcEnforcementPolicyTest, AuthorizeOperationRejectsExpiredKey) {
  const ::keymaster::km_id_t kKeyId = 0xdeadbeef;

  ::keymaster::AuthorizationSet hw, sw;
  hw.push_back(::keymaster::TAG_ALGORITHM, KM_ALGORITHM_HMAC);
  hw.push_back(::keymaster::TAG_PURPOSE, KM_PURPOSE_SIGN);
  hw.push_back(::keymaster::TAG_NO_AUTH_REQUIRED);
  uint64_t past_date = static_cast<uint64_t>(
      (base::Time::Now() - base::Days(30)).InMillisecondsSinceUnixEpoch());
  hw.push_back(::keymaster::TAG_ORIGINATION_EXPIRE_DATETIME, past_date);

  ::keymaster::AuthorizationSet op_params;
  keymaster_error_t result = arc_enforcement_policy_->AuthorizeOperation(
      KM_PURPOSE_SIGN, kKeyId, ::keymaster::AuthProxy(hw, sw), op_params,
      /*op_handle=*/0, /*is_begin_operation=*/true);

  EXPECT_EQ(result, KM_ERROR_KEY_EXPIRED);
}

}  // namespace arc::keymint::context
