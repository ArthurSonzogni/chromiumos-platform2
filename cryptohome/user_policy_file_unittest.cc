// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_policy_file.h"

#include <string>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_platform.h"

namespace cryptohome {
namespace {

using ::brillo::Blob;
using ::cryptohome::SerializedUserAuthFactorTypePolicy;
using ::cryptohome::SerializedUserPolicy;

class UserPolicyFileTest : public ::testing::Test {
 protected:
  const ObfuscatedUsername kObfuscatedUsername{"foo@gmail.com"};
  const std::string kTestFile = "FlatbufferTestFile";

  MockPlatform platform_;
  UserPolicyFile user_policy_file_{
      &platform_, UserPath(kObfuscatedUsername).Append(kTestFile)};
};

void CheckUserPolicyEq(const SerializedUserPolicy& serialized_user_policy1,
                       const SerializedUserPolicy& serialized_user_policy2) {
  EXPECT_EQ(serialized_user_policy1.auth_factor_type_policy.size(),
            serialized_user_policy2.auth_factor_type_policy.size());
  for (int i = 0; i < serialized_user_policy1.auth_factor_type_policy.size();
       i++) {
    SerializedUserAuthFactorTypePolicy policy1 =
        serialized_user_policy1.auth_factor_type_policy[i];
    SerializedUserAuthFactorTypePolicy policy2 =
        serialized_user_policy2.auth_factor_type_policy[i];
    EXPECT_EQ(policy1.type, policy2.type);
    EXPECT_EQ(policy1.enabled_intents.size(), policy2.enabled_intents.size());
    EXPECT_EQ(policy1.disabled_intents.size(), policy2.disabled_intents.size());
    for (int j = 0; j < policy1.enabled_intents.size(); j++) {
      EXPECT_EQ(policy1.enabled_intents[j], policy2.enabled_intents[j]);
    }
    for (int j = 0; j < policy1.disabled_intents.size(); j++) {
      EXPECT_EQ(policy1.disabled_intents[j], policy2.disabled_intents[j]);
    }
  }
}

TEST_F(UserPolicyFileTest, StoreAndLoad) {
  SerializedUserAuthFactorTypePolicy user_auth_factor_type_policy1(
      {.type = SerializedAuthFactorType::kPassword,
       .enabled_intents = {SerializedAuthIntent::kDecrypt},
       .disabled_intents = {SerializedAuthIntent::kWebAuthn}});
  SerializedUserAuthFactorTypePolicy user_auth_factor_type_policy2(
      {.type = SerializedAuthFactorType::kPin,
       .enabled_intents = {SerializedAuthIntent::kVerifyOnly},
       .disabled_intents = {SerializedAuthIntent::kWebAuthn}});
  SerializedUserPolicy serialized_user_policy(
      {.auth_factor_type_policy = {user_auth_factor_type_policy1,
                                   user_auth_factor_type_policy2}});
  user_policy_file_.UpdateUserPolicy(serialized_user_policy);
  auto store_status = user_policy_file_.StoreInFile();
  ASSERT_TRUE(store_status.ok());
  auto load_status = user_policy_file_.LoadFromFile();
  ASSERT_TRUE(load_status.ok());
  std::optional<SerializedUserPolicy> read_user_policy =
      user_policy_file_.GetUserPolicy();
  ASSERT_TRUE(read_user_policy.has_value());
  CheckUserPolicyEq(serialized_user_policy, read_user_policy.value());
}

}  // namespace

}  // namespace cryptohome
