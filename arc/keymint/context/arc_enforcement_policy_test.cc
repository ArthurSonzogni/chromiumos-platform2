// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_enforcement_policy.h"

#include <memory>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "absl/strings/escaping.h"

namespace arc::keymint::context {

namespace {

constexpr char kSecretKey[] = "Fake Secret Key";
constexpr char kInputData[] = "I am fake data to be used\n";
constexpr char kExpectedHmacSha256Hex[] =
    "465b0bcb59d8da7b7f9ce006ad21f094e531c06b0a3eb2314715fcda2970c8ed";
}  // namespace

class ArcEnforcementPolicyTest : public ::testing::Test {
 protected:
  ArcEnforcementPolicyTest() {}

  void SetUp() override {
    arc_enforcement_policy_ = new ArcEnforcementPolicy(64, 64);
  }

  ArcEnforcementPolicy* arc_enforcement_policy_;
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

}  // namespace arc::keymint::context
