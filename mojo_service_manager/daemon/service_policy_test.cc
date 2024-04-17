// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "mojo_service_manager/daemon/service_policy.h"
#include "mojo_service_manager/daemon/service_policy_test_util.h"

namespace chromeos::mojo_service_manager {
namespace {

TEST(ServicePolicyTest, OwnerUid) {
  ServicePolicy policy;
  EXPECT_FALSE(policy.IsOwnerUid(123));
  EXPECT_EQ(policy.owner_uid(), std::nullopt);
  policy.SetOwnerUid(123);
  EXPECT_TRUE(policy.IsOwnerUid(123));
  EXPECT_FALSE(policy.IsOwnerUid(456));
  EXPECT_EQ(policy.owner_uid(), 123);
}

TEST(ServicePolicyTest, RequesterUid) {
  ServicePolicy policy;
  policy.AddRequesterUid(123);
  EXPECT_TRUE(policy.IsRequesterUid(123));
  EXPECT_FALSE(policy.IsRequesterUid(456));
  EXPECT_EQ(policy.requesters_uid(), std::set<uint32_t>{123});
}

TEST(ServicePolicyTest, Default) {
  ServicePolicy policy;
  // Test owner.
  EXPECT_FALSE(policy.IsOwner("owner"));
  policy.SetOwner("owner");
  EXPECT_TRUE(policy.IsOwner("owner"));
  EXPECT_FALSE(policy.IsOwner("not_an_owner"));

  // Test requester.
  policy.AddRequester("requester");
  EXPECT_TRUE(policy.IsRequester("requester"));
  EXPECT_FALSE(policy.IsRequester("not_a_requester"));
  EXPECT_EQ(policy.requesters(), std::set<std::string>{"requester"});
}

TEST(ServicePolicyTest, MergeUid) {
  ServicePolicy policy;
  EXPECT_TRUE(policy.Merge(CreateServicePolicyForTest(/*owner=*/std::nullopt,
                                                      /*requesters=*/{4})));
  EXPECT_FALSE(policy.owner_uid());
  EXPECT_TRUE(policy.IsRequesterUid(4));

  EXPECT_TRUE(policy.Merge(
      CreateServicePolicyForTest(/*owner=*/1, /*requesters=*/{5})));
  EXPECT_TRUE(policy.IsOwnerUid(1));
  EXPECT_TRUE(policy.IsRequesterUid(5));

  // Merge will fail because owner has been set.
  EXPECT_FALSE(policy.Merge(
      CreateServicePolicyForTest(/*owner=*/2, /*requesters=*/{6})));
  EXPECT_TRUE(policy.IsRequesterUid(6));
}

TEST(ServicePolicyTest, Merge) {
  ServicePolicy policy;
  EXPECT_TRUE(policy.Merge(CreateServicePolicyForTest("", {"requester_a"})));
  EXPECT_TRUE(policy.owner().empty());
  EXPECT_TRUE(policy.IsRequester("requester_a"));

  EXPECT_TRUE(
      policy.Merge(CreateServicePolicyForTest("owner_a", {"requester_b"})));
  EXPECT_TRUE(policy.IsOwner("owner_a"));
  EXPECT_TRUE(policy.IsRequester("requester_b"));

  // Merge will fail because owner has been set.
  EXPECT_FALSE(
      policy.Merge(CreateServicePolicyForTest("owner_b", {"requester_c"})));
  EXPECT_TRUE(policy.IsRequester("requester_c"));
}

TEST(ServicePolicyTest, MergeUidOwnerAndLegacyOwner) {
  ServicePolicy policy;
  EXPECT_TRUE(
      policy.Merge(CreateServicePolicyForTest(/*owner=*/1, /*requesters=*/{})));
  EXPECT_EQ(policy.owner_uid(), 1);
  EXPECT_TRUE(policy.owner().empty());

  // Merge will fail because owner has been set.
  EXPECT_FALSE(policy.Merge(CreateServicePolicyForTest("owner", {})));
}

TEST(ServicePolicyTest, MergeUidRequesterAndLegacyRequester) {
  ServicePolicy policy;
  EXPECT_TRUE(policy.Merge(CreateServicePolicyForTest(/*owner=*/std::nullopt,
                                                      /*requesters=*/{1})));
  EXPECT_TRUE(policy.Merge(CreateServicePolicyForTest("", {"requester_a"})));
  EXPECT_TRUE(policy.IsRequester("requester_a"));
  EXPECT_TRUE(policy.IsRequesterUid(1));
}

TEST(ServicePolicyTest, MergeUidServicePolicyMaps) {
  auto from = CreateServicePolicyMapForTest({
      {"ServiceA", {/*owner=*/1, /*requesters=*/{2, 3}}},
      {"ServiceB", {/*owner=*/1, /*requesters=*/{2, 3}}},
  });
  auto to = CreateServicePolicyMapForTest({
      {"ServiceA", {/*owner=*/std::nullopt, /*requesters=*/{3, 4}}},
      {"ServiceC", {/*owner=*/1, /*requesters=*/{3, 4}}},
  });
  EXPECT_TRUE(MergeServicePolicyMaps(&from, &to));
  EXPECT_EQ(to, CreateServicePolicyMapForTest({
                    {"ServiceA", {/*owner=*/1, /*requesters=*/{2, 3, 4}}},
                    {"ServiceB", {/*owner=*/1, /*requesters=*/{2, 3}}},
                    {"ServiceC", {/*owner=*/1, /*requesters=*/{3, 4}}},
                }));
  // "ServiceA" sets owner twice, so the merge will return false but the
  // requester are still merged.
  from = CreateServicePolicyMapForTest({
      {"ServiceA", {1, {5}}},
  });
  EXPECT_FALSE(MergeServicePolicyMaps(&from, &to));
  EXPECT_EQ(to, CreateServicePolicyMapForTest({
                    {"ServiceA", {/*owner=*/1, /*requesters=*/{2, 3, 4, 5}}},
                    {"ServiceB", {/*owner=*/1, /*requesters=*/{2, 3}}},
                    {"ServiceC", {/*owner=*/1, /*requesters=*/{3, 4}}},
                }));
}

TEST(ServicePolicyTest, MergeServicePolicyMaps) {
  auto from = CreateServicePolicyMapForTest({
      {"ServiceA", {"owner_a", {"requester_a", "requester_b"}}},
      {"ServiceB", {"owner_a", {"requester_a", "requester_b"}}},
  });
  auto to = CreateServicePolicyMapForTest({
      {"ServiceA", {"", {"requester_b", "requester_c"}}},
      {"ServiceC", {"owner_a", {"requester_b", "requester_c"}}},
  });
  EXPECT_TRUE(MergeServicePolicyMaps(&from, &to));
  EXPECT_EQ(to,
            CreateServicePolicyMapForTest({
                {"ServiceA",
                 {"owner_a", {"requester_a", "requester_b", "requester_c"}}},
                {"ServiceB", {"owner_a", {"requester_a", "requester_b"}}},
                {"ServiceC", {"owner_a", {"requester_b", "requester_c"}}},
            }));
  // "ServiceA" sets owner twice, so the merge will return false but the
  // requester are still merged.
  from = CreateServicePolicyMapForTest({
      {"ServiceA", {"owner_a", {"requester_d"}}},
  });
  EXPECT_FALSE(MergeServicePolicyMaps(&from, &to));
  EXPECT_EQ(
      to, CreateServicePolicyMapForTest({
              {"ServiceA",
               {"owner_a",
                {"requester_a", "requester_b", "requester_c", "requester_d"}}},
              {"ServiceB", {"owner_a", {"requester_a", "requester_b"}}},
              {"ServiceC", {"owner_a", {"requester_b", "requester_c"}}},
          }));
}

TEST(ServicePolicyTest, ValidateSecurityContext) {
  EXPECT_TRUE(ValidateSecurityContext("a"));
  EXPECT_TRUE(ValidateSecurityContext("system_u:object_r:cros_t:s0"));

  // Empty.
  EXPECT_FALSE(ValidateSecurityContext(""));
  // No space.
  EXPECT_FALSE(ValidateSecurityContext("a b"));
  // No uppercase.
  EXPECT_FALSE(ValidateSecurityContext("A"));
  // No '-'.
  EXPECT_FALSE(ValidateSecurityContext("a-b"));
}

TEST(ServicePolicyTest, ValidateServiceName) {
  EXPECT_TRUE(ValidateServiceName("FooServiceName"));

  // Empty.
  EXPECT_FALSE(ValidateServiceName(""));
  // No space.
  EXPECT_FALSE(ValidateServiceName("a b"));
  // No these chars: ":_.-".
  EXPECT_FALSE(ValidateServiceName("a:b"));
  EXPECT_FALSE(ValidateServiceName("a_b"));
  EXPECT_FALSE(ValidateServiceName("a.b"));
  EXPECT_FALSE(ValidateServiceName("a-b"));
}

}  // namespace
}  // namespace chromeos::mojo_service_manager
