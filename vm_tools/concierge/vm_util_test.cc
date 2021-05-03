// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_util.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vm_tools {
namespace concierge {

TEST(VMUtilTest, LoadCustomParametersSupportsEmptyInput) {
  base::StringPairs args;
  LoadCustomParameters("", &args);
  base::StringPairs expected;
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersParsesManyPairs) {
  base::StringPairs args;
  LoadCustomParameters("Key1=Value1\nKey2=Value2\nKey3=Value3", &args);
  base::StringPairs expected = {
      {"Key1", "Value1"}, {"Key2", "Value2"}, {"Key3", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSkipsComments) {
  base::StringPairs args;
  LoadCustomParameters("Key1=Value1\n#Key2=Value2\nKey3=Value3", &args);
  base::StringPairs expected{{"Key1", "Value1"}, {"Key3", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSkipsEmptyLines) {
  base::StringPairs args;
  LoadCustomParameters("Key1=Value1\n\n\n\n\n\n\nKey2=Value2\n\n\n\n", &args);
  base::StringPairs expected{{"Key1", "Value1"}, {"Key2", "Value2"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSupportsKeyWithoutValue) {
  base::StringPairs args;
  LoadCustomParameters("Key1=Value1\nKey2\n\n\n\nKey3", &args);
  base::StringPairs expected{{"Key1", "Value1"}, {"Key2", ""}, {"Key3", ""}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSupportsRemoving) {
  base::StringPairs args = {{"KeyToBeReplaced", "OldValue"},
                            {"KeyToBeKept", "ValueToBeKept"}};
  LoadCustomParameters(
      "Key1=Value1\nKey2=Value2\n!KeyToBeReplaced\nKeyToBeReplaced=NewValue",
      &args);
  base::StringPairs expected{{"KeyToBeKept", "ValueToBeKept"},
                             {"Key1", "Value1"},
                             {"Key2", "Value2"},
                             {"KeyToBeReplaced", "NewValue"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSupportsRemovingByPrefix) {
  base::StringPairs args = {{"foo", ""},
                            {"foo", "bar"},
                            {"foobar", ""},
                            {"foobar", "baz"},
                            {"barfoo", ""}};
  LoadCustomParameters("!foo", &args);
  base::StringPairs expected{{"barfoo", ""}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, RemoveParametersWithKeyReturnsFoundValue) {
  base::StringPairs args = {{"KERNEL_PATH", "/a/b/c"}, {"Key1", "Value1"}};
  LoadCustomParameters("Key2=Value2\nKey3=Value3", &args);
  const std::string resolved_kernel_path =
      RemoveParametersWithKey("KERNEL_PATH", "default_path", &args);

  base::StringPairs expected{
      {"Key1", "Value1"}, {"Key2", "Value2"}, {"Key3", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
  EXPECT_THAT(resolved_kernel_path, "/a/b/c");
}

TEST(VMUtilTest, RemoveParametersWithKeyReturnsDefaultValue) {
  base::StringPairs args = {{"SOME_OTHER_PATH", "/a/b/c"}, {"Key1", "Value1"}};
  LoadCustomParameters("Key2=Value2\nKey3=Value3", &args);
  const std::string resolved_kernel_path =
      RemoveParametersWithKey("KERNEL_PATH", "default_path", &args);

  base::StringPairs expected{{"SOME_OTHER_PATH", "/a/b/c"},
                             {"Key1", "Value1"},
                             {"Key2", "Value2"},
                             {"Key3", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
  EXPECT_THAT(resolved_kernel_path, "default_path");
}

TEST(VMUtilTest, GetCpuAffinityFromClustersNoGroups) {
  std::vector<std::vector<std::string>> cpu_clusters;
  std::map<int32_t, std::vector<std::string>> cpu_capacity_groups;

  auto cpu_affinity =
      GetCpuAffinityFromClusters(cpu_clusters, cpu_capacity_groups);
  EXPECT_EQ(cpu_affinity, base::nullopt);
}

TEST(VMUtilTest, GetCpuAffinityFromClustersGroupSizesOne) {
  std::vector<std::vector<std::string>> cpu_clusters;
  std::map<int32_t, std::vector<std::string>> cpu_capacity_groups;

  cpu_clusters.push_back({"0", "1", "2", "3"});

  cpu_capacity_groups.insert({1024, {"0", "1", "2", "3"}});

  auto cpu_affinity =
      GetCpuAffinityFromClusters(cpu_clusters, cpu_capacity_groups);
  EXPECT_EQ(cpu_affinity, base::nullopt);
}

TEST(VMUtilTest, GetCpuAffinityFromClustersTwoClusters) {
  std::vector<std::vector<std::string>> cpu_clusters;
  std::map<int32_t, std::vector<std::string>> cpu_capacity_groups;

  cpu_clusters.push_back({"0", "1"});
  cpu_clusters.push_back({"2", "3"});

  cpu_capacity_groups.insert({1024, {"0", "1", "2", "3"}});

  auto cpu_affinity =
      GetCpuAffinityFromClusters(cpu_clusters, cpu_capacity_groups);
  ASSERT_TRUE(cpu_affinity);
  EXPECT_EQ(*cpu_affinity, "0=0,1:1=0,1:2=2,3:3=2,3");
}

TEST(VMUtilTest, GetCpuAffinityFromClustersTwoCapacityGroups) {
  std::vector<std::vector<std::string>> cpu_clusters;
  std::map<int32_t, std::vector<std::string>> cpu_capacity_groups;

  cpu_clusters.push_back({"0", "1", "2", "3"});

  cpu_capacity_groups.insert({100, {"0", "2"}});
  cpu_capacity_groups.insert({200, {"1", "3"}});

  auto cpu_affinity =
      GetCpuAffinityFromClusters(cpu_clusters, cpu_capacity_groups);
  ASSERT_TRUE(cpu_affinity);
  EXPECT_EQ(*cpu_affinity, "0=0,2:2=0,2:1=1,3:3=1,3");
}

TEST(VMUtilTest, GetCpuAffinityFromClustersBothPresent) {
  std::vector<std::vector<std::string>> cpu_clusters;
  std::map<int32_t, std::vector<std::string>> cpu_capacity_groups;

  cpu_clusters.push_back({"0", "1"});
  cpu_clusters.push_back({"2", "3"});

  cpu_capacity_groups.insert({100, {"0", "2"}});
  cpu_capacity_groups.insert({200, {"1", "3"}});

  auto cpu_affinity =
      GetCpuAffinityFromClusters(cpu_clusters, cpu_capacity_groups);
  ASSERT_TRUE(cpu_affinity);
  // Clusters take precedence over capacity groups, so this matches the
  // TwoClusters result.
  EXPECT_EQ(*cpu_affinity, "0=0,1:1=0,1:2=2,3:3=2,3");
}

}  // namespace concierge
}  // namespace vm_tools
