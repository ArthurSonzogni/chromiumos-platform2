// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_util.h"

#include <cstring>
#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "net-base/ipv4_address.h"
#include "vm_tools/concierge/fake_crosvm_control.h"

using testing::Exactly;
using testing::NotNull;
using testing::Return;
using testing::StrEq;

namespace vm_tools {
namespace concierge {
namespace {

void LoadCustomParameters(const std::string& data, base::StringPairs& args) {
  CustomParametersForDev custom(data);
  custom.Apply(args);
}

}  // namespace

TEST(VMUtilTest, LoadCustomParametersSupportsEmptyInput) {
  base::StringPairs args;
  LoadCustomParameters("", args);
  base::StringPairs expected;
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersParsesManyPairs) {
  base::StringPairs args;
  LoadCustomParameters(R"(--Key1=Value1
--Key2=Value2
--Key3=Value3)",
                       args);
  base::StringPairs expected = {
      {"--Key1", "Value1"}, {"--Key2", "Value2"}, {"--Key3", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSkipsComments) {
  base::StringPairs args;
  LoadCustomParameters(R"(--Key1=Value1
#--Key2=Value2
--Key3=Value3)",
                       args);
  base::StringPairs expected{{"--Key1", "Value1"}, {"--Key3", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSkipsEmptyLines) {
  base::StringPairs args;
  LoadCustomParameters(R"(--Key1=Value1




--Key2=Value2



)",
                       args);
  base::StringPairs expected{{"--Key1", "Value1"}, {"--Key2", "Value2"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSupportsKeyWithoutValue) {
  base::StringPairs args;
  LoadCustomParameters(R"(--Key1=Value1
--Key2



--Key3)",
                       args);
  base::StringPairs expected{
      {"--Key1", "Value1"}, {"--Key2", ""}, {"--Key3", ""}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSupportsPrepend) {
  base::StringPairs args = {{"--KeyToBeSecond", "Value1"},
                            {"--KeyToBeThird", "Value2"}};
  LoadCustomParameters(
      R"(--AppendKey=Value3
^--PrependKey=Value0
)",
      args);
  base::StringPairs expected{{"--PrependKey", "Value0"},
                             {"--KeyToBeSecond", "Value1"},
                             {"--KeyToBeThird", "Value2"},
                             {"--AppendKey", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSupportsRemoving) {
  base::StringPairs args = {{"--KeyToBeReplaced", "OldValue"},
                            {"--KeyToBeKept", "ValueToBeKept"}};
  LoadCustomParameters(
      R"(--Key1=Value1
--Key2=Value2
!--KeyToBeReplaced
--KeyToBeReplaced=NewValue1
^--KeyToBeReplaced=NewValue2)",
      args);
  base::StringPairs expected{{"--KeyToBeReplaced", "NewValue2"},
                             {"--KeyToBeKept", "ValueToBeKept"},
                             {"--Key1", "Value1"},
                             {"--Key2", "Value2"},
                             {"--KeyToBeReplaced", "NewValue1"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(VMUtilTest, LoadCustomParametersSupportsRemovingByPrefix) {
  base::StringPairs args = {{"foo", ""},
                            {"foo", "bar"},
                            {"foobar", ""},
                            {"foobar", "baz"},
                            {"barfoo", ""}};
  LoadCustomParameters("!foo", args);
  base::StringPairs expected{{"barfoo", ""}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
}

TEST(CustomParametersForDevTest, KernelWithCustom) {
  base::StringPairs args = {{"--Key1", "Value1"}};
  CustomParametersForDev custom(R"(--Key2=Value2
KERNEL_PATH=/a/b/c
--Key3=Value3)");
  custom.Apply(args);
  const std::string resolved_kernel_path =
      custom.ObtainSpecialParameter("KERNEL_PATH").value_or("default_path");

  base::StringPairs expected{
      {"--Key1", "Value1"}, {"--Key2", "Value2"}, {"--Key3", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
  EXPECT_THAT(resolved_kernel_path, "/a/b/c");
}

TEST(CustomParametersForDevTest, KernelWithMultipleCustomLastTakesEffect) {
  base::StringPairs args = {{"--Key1", "Value1"}};
  CustomParametersForDev custom(R"(--Key2=Value2
KERNEL_PATH=/a/b/c
KERNEL_PATH=/d/e/f
--Key3=Value3)");
  custom.Apply(args);
  const std::string resolved_kernel_path =
      custom.ObtainSpecialParameter("KERNEL_PATH").value_or("default_path");

  // Just check what order they were parsed
  const auto kernel_paths = custom.ObtainSpecialParameters("KERNEL_PATH");
  EXPECT_EQ(kernel_paths[0], "/a/b/c");
  EXPECT_EQ(kernel_paths[1], "/d/e/f");
  EXPECT_EQ(kernel_paths.size(), 2);

  base::StringPairs expected{
      {"--Key1", "Value1"}, {"--Key2", "Value2"}, {"--Key3", "Value3"}};
  EXPECT_THAT(args, testing::ContainerEq(expected));
  EXPECT_THAT(resolved_kernel_path, "/d/e/f");
}

TEST(CustomParametersForDevTest, KernelWithDefault) {
  base::StringPairs args = {{"--Key1", "Value1"}};
  CustomParametersForDev custom(R"(--Key2=Value2
--Key3=Value3
SOME_OTHER_PATH=/a/b/c)");
  custom.Apply(args);
  const std::string resolved_kernel_path =
      custom.ObtainSpecialParameter("KERNEL_PATH").value_or("default_path");

  base::StringPairs expected{
      {"--Key1", "Value1"},
      {"--Key2", "Value2"},
      {"--Key3", "Value3"},
  };
  EXPECT_THAT(args, testing::ContainerEq(expected));
  EXPECT_THAT(resolved_kernel_path, "default_path");
}

TEST(CustomParametersForDevTest, BlockMultipleWorkers) {
  base::StringPairs args = {{"--Key1", "Value1"}};
  CustomParametersForDev custom(R"(BLOCK_MULTIPLE_WORKERS=true)");
  custom.Apply(args);
  const std::string multiple_workers =
      custom.ObtainSpecialParameter("BLOCK_MULTIPLE_WORKERS").value_or("false");

  base::StringPairs expected{
      {"--Key1", "Value1"},
  };
  EXPECT_THAT(args, testing::ContainerEq(expected));
  EXPECT_THAT(multiple_workers, "true");
}

TEST(CustomParametersForDevTest, BlockAsyncExecutor) {
  base::StringPairs args = {{"--Key1", "Value1"}};
  CustomParametersForDev custom(R"(BLOCK_ASYNC_EXECUTOR=uring)");
  custom.Apply(args);
  const std::string block_async_executor =
      custom.ObtainSpecialParameter("BLOCK_ASYNC_EXECUTOR").value_or("epoll");

  base::StringPairs expected{
      {"--Key1", "Value1"},
  };
  EXPECT_THAT(args, testing::ContainerEq(expected));
  EXPECT_THAT(block_async_executor, "uring");
}

TEST(VMUtilTest, GetCpuAffinityFromClustersNoGroups) {
  std::vector<std::vector<std::string>> cpu_clusters;
  std::map<int32_t, std::vector<std::string>> cpu_capacity_groups;

  auto cpu_affinity =
      GetCpuAffinityFromClusters(cpu_clusters, cpu_capacity_groups);
  EXPECT_EQ(cpu_affinity, std::nullopt);
}

TEST(VMUtilTest, GetCpuAffinityFromClustersGroupSizesOne) {
  std::vector<std::vector<std::string>> cpu_clusters;
  std::map<int32_t, std::vector<std::string>> cpu_capacity_groups;

  cpu_clusters.push_back({"0", "1", "2", "3"});

  cpu_capacity_groups.insert({1024, {"0", "1", "2", "3"}});

  auto cpu_affinity =
      GetCpuAffinityFromClusters(cpu_clusters, cpu_capacity_groups);
  EXPECT_EQ(cpu_affinity, std::nullopt);
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

// CPU0-CPU1 LITTLE cores, CPU2-CPU3 big cores
TEST(VMUtilTest, CreateArcVMAffinityTwoGroups) {
  ArcVmCPUTopology topology(4, 1);

  topology.AddCpuToCapacityGroupForTesting(0, 42);
  topology.AddCpuToCapacityGroupForTesting(1, 42);
  topology.AddCpuToCapacityGroupForTesting(2, 128);
  topology.AddCpuToCapacityGroupForTesting(3, 128);
  topology.AddCpuToPackageGroupForTesting(0, 0);
  topology.AddCpuToPackageGroupForTesting(1, 0);
  topology.AddCpuToPackageGroupForTesting(2, 1);
  topology.AddCpuToPackageGroupForTesting(3, 1);
  topology.CreateCPUAffinityForTesting();

  EXPECT_EQ(topology.NumCPUs(), 5);
  EXPECT_EQ(topology.NumRTCPUs(), 1);
  EXPECT_EQ(topology.RTCPUMask(), "4");
  EXPECT_EQ(topology.NonRTCPUMask(), "0,1,2,3");
  EXPECT_FALSE(topology.IsSymmetricCPU());
  EXPECT_EQ(topology.AffinityMask(), "0=0,1:1=0,1:4=0,1:2=2,3:3=2,3");
  EXPECT_EQ(topology.CapacityMask(), "0=42,1=42,2=128,3=128,4=42");

  auto& package = topology.PackageMask();
  ASSERT_EQ(package.size(), 2);
  EXPECT_EQ(package[0], "0,1,4");
  EXPECT_EQ(package[1], "2,3");
}

TEST(VMUtilTest, CreateArcVMAffinityOnePackage) {
  ArcVmCPUTopology topology(4, 1);

  topology.AddCpuToCapacityGroupForTesting(0, 42);
  topology.AddCpuToCapacityGroupForTesting(1, 42);
  topology.AddCpuToCapacityGroupForTesting(2, 128);
  topology.AddCpuToCapacityGroupForTesting(3, 128);
  topology.AddCpuToPackageGroupForTesting(0, 0);
  topology.AddCpuToPackageGroupForTesting(1, 0);
  topology.AddCpuToPackageGroupForTesting(2, 0);
  topology.AddCpuToPackageGroupForTesting(3, 0);
  topology.CreateCPUAffinityForTesting();

  EXPECT_EQ(topology.NumCPUs(), 5);
  EXPECT_EQ(topology.NumRTCPUs(), 1);
  EXPECT_EQ(topology.RTCPUMask(), "4");
  EXPECT_EQ(topology.NonRTCPUMask(), "0,1,2,3");
  EXPECT_EQ(topology.AffinityMask(), "0=0,1:1=0,1:4=0,1:2=2,3:3=2,3");
  EXPECT_EQ(topology.CapacityMask(), "0=42,1=42,2=128,3=128,4=42");

  auto& package = topology.PackageMask();
  ASSERT_EQ(package.size(), 1);
  EXPECT_EQ(package[0], "0,1,2,3,4");
}

TEST(VMUtilTest, CreateArcVMAffinityOnePackageOneCapacity) {
  ArcVmCPUTopology topology(4, 1);

  topology.AddCpuToCapacityGroupForTesting(0, 42);
  topology.AddCpuToCapacityGroupForTesting(1, 42);
  topology.AddCpuToCapacityGroupForTesting(2, 42);
  topology.AddCpuToCapacityGroupForTesting(3, 42);
  topology.AddCpuToPackageGroupForTesting(0, 0);
  topology.AddCpuToPackageGroupForTesting(1, 0);
  topology.AddCpuToPackageGroupForTesting(2, 0);
  topology.AddCpuToPackageGroupForTesting(3, 0);
  topology.CreateCPUAffinityForTesting();

  EXPECT_EQ(topology.NumCPUs(), 5);
  EXPECT_EQ(topology.NumRTCPUs(), 1);
  EXPECT_EQ(topology.RTCPUMask(), "4");
  EXPECT_EQ(topology.NonRTCPUMask(), "0,1,2,3");
  EXPECT_TRUE(topology.IsSymmetricCPU());
  EXPECT_EQ(topology.CapacityMask(), "0=42,1=42,2=42,3=42,4=42");

  auto& package = topology.PackageMask();
  ASSERT_EQ(package.size(), 1);
  EXPECT_EQ(package[0], "0,1,2,3,4");
}

// CPU2-CPU3 LITTLE cores, CPU0-CPU1 big cores
TEST(VMUtilTest, CreateArcVMAffinityTwoCapacityClustersReverse) {
  ArcVmCPUTopology topology(4, 1);

  topology.AddCpuToCapacityGroupForTesting(2, 42);
  topology.AddCpuToCapacityGroupForTesting(3, 42);
  topology.AddCpuToCapacityGroupForTesting(0, 128);
  topology.AddCpuToCapacityGroupForTesting(1, 128);
  topology.AddCpuToPackageGroupForTesting(0, 0);
  topology.AddCpuToPackageGroupForTesting(1, 0);
  topology.AddCpuToPackageGroupForTesting(2, 0);
  topology.AddCpuToPackageGroupForTesting(3, 0);
  topology.CreateCPUAffinityForTesting();

  EXPECT_EQ(topology.NumCPUs(), 5);
  EXPECT_EQ(topology.NumRTCPUs(), 1);
  EXPECT_EQ(topology.RTCPUMask(), "4");
  EXPECT_EQ(topology.NonRTCPUMask(), "0,1,2,3");
  EXPECT_FALSE(topology.IsSymmetricCPU());
  EXPECT_EQ(topology.AffinityMask(), "2=2,3:3=2,3:4=2,3:0=0,1:1=0,1");
  EXPECT_EQ(topology.CapacityMask(), "2=42,3=42,0=128,1=128,4=42");

  auto& package = topology.PackageMask();
  ASSERT_EQ(package.size(), 1);
  EXPECT_EQ(package[0], "0,1,2,3,4");
}

// All cores are in the same capacity group
TEST(VMUtilTest, CreateArcVMAffinityOneCapacityCluster) {
  ArcVmCPUTopology topology(4, 1);

  topology.AddCpuToCapacityGroupForTesting(0, 42);
  topology.AddCpuToCapacityGroupForTesting(1, 42);
  topology.AddCpuToCapacityGroupForTesting(2, 42);
  topology.AddCpuToCapacityGroupForTesting(3, 42);
  topology.AddCpuToPackageGroupForTesting(0, 0);
  topology.AddCpuToPackageGroupForTesting(1, 0);
  topology.AddCpuToPackageGroupForTesting(2, 0);
  topology.AddCpuToPackageGroupForTesting(3, 0);
  topology.CreateCPUAffinityForTesting();

  EXPECT_EQ(topology.NumCPUs(), 5);
  EXPECT_EQ(topology.NumRTCPUs(), 1);
  EXPECT_EQ(topology.RTCPUMask(), "4");
  EXPECT_EQ(topology.NonRTCPUMask(), "0,1,2,3");
  EXPECT_TRUE(topology.IsSymmetricCPU());
  EXPECT_EQ(topology.CapacityMask(), "0=42,1=42,2=42,3=42,4=42");

  auto& package = topology.PackageMask();
  ASSERT_EQ(package.size(), 1);
  EXPECT_EQ(package[0], "0,1,2,3,4");
}

// No RT CPU requested
TEST(VMUtilTest, CreateArcVMAffinityOneCapacityClusterNoRT) {
  ArcVmCPUTopology topology(4, 0);

  topology.AddCpuToCapacityGroupForTesting(0, 42);
  topology.AddCpuToCapacityGroupForTesting(1, 42);
  topology.AddCpuToCapacityGroupForTesting(2, 42);
  topology.AddCpuToCapacityGroupForTesting(3, 42);
  topology.AddCpuToPackageGroupForTesting(0, 0);
  topology.AddCpuToPackageGroupForTesting(1, 0);
  topology.AddCpuToPackageGroupForTesting(2, 0);
  topology.AddCpuToPackageGroupForTesting(3, 0);
  topology.CreateCPUAffinityForTesting();

  ASSERT_EQ(topology.RTCPUMask().size(), 0);
  EXPECT_EQ(topology.NumCPUs(), 4);
  EXPECT_EQ(topology.NumRTCPUs(), 0);
  EXPECT_TRUE(topology.IsSymmetricCPU());
  EXPECT_EQ(topology.CapacityMask(), "0=42,1=42,2=42,3=42");

  auto& package = topology.PackageMask();
  ASSERT_EQ(package.size(), 1);
  EXPECT_EQ(package[0], "0,1,2,3");
}

// SMP cores without capacities.
TEST(VMUtilTest, CreateArcVMAffinitySMP2Core) {
  ArcVmCPUTopology topology(2, 1);

  topology.AddCpuToCapacityGroupForTesting(0, 0);
  topology.AddCpuToCapacityGroupForTesting(1, 0);
  topology.AddCpuToPackageGroupForTesting(0, 0);
  topology.AddCpuToPackageGroupForTesting(1, 0);
  topology.CreateCPUAffinityForTesting();

  EXPECT_EQ(topology.NumCPUs(), 3);
  EXPECT_EQ(topology.NumRTCPUs(), 1);
  ASSERT_EQ(topology.RTCPUMask(), "2");
  EXPECT_EQ(topology.NonRTCPUMask(), "0,1");
  EXPECT_TRUE(topology.IsSymmetricCPU());

  auto& package = topology.PackageMask();
  ASSERT_EQ(package.size(), 1);
  EXPECT_EQ(package[0], "0,1,2");
}

TEST(VMUtilTest, CreateArcVMAffinitySMP4Core) {
  ArcVmCPUTopology topology(4, 1);

  topology.AddCpuToCapacityGroupForTesting(0, 0);
  topology.AddCpuToCapacityGroupForTesting(1, 0);
  topology.AddCpuToCapacityGroupForTesting(2, 0);
  topology.AddCpuToCapacityGroupForTesting(3, 0);
  topology.AddCpuToPackageGroupForTesting(0, 0);
  topology.AddCpuToPackageGroupForTesting(1, 0);
  topology.AddCpuToPackageGroupForTesting(2, 0);
  topology.AddCpuToPackageGroupForTesting(3, 0);
  topology.CreateCPUAffinityForTesting();

  EXPECT_EQ(topology.NumCPUs(), 5);
  EXPECT_EQ(topology.NumRTCPUs(), 1);
  ASSERT_EQ(topology.RTCPUMask(), "4");
  EXPECT_EQ(topology.NonRTCPUMask(), "0,1,2,3");
  EXPECT_TRUE(topology.IsSymmetricCPU());

  auto& package = topology.PackageMask();
  ASSERT_EQ(package.size(), 1);
  EXPECT_EQ(package[0], "0,1,2,3,4");
}

TEST(VMUtilTest, SharedDataParamCacheAlways) {
  SharedDataParam param{.data_dir = base::FilePath("/usr/local/bin"),
                        .tag = "usr_local_bin",
                        .uid_map = kAndroidUidMap,
                        .gid_map = kAndroidGidMap,
                        .enable_caches = SharedDataParam::Cache::kAlways,
                        .ascii_casefold = false,
                        .posix_acl = true};
  ASSERT_EQ(param.to_string(),
            "/usr/local/bin:usr_local_bin:type=fs:cache=always:uidmap=0 655360 "
            "5000,5000 600 50,5050 660410 1994950:gidmap=0 655360 1065,1065 "
            "20119 1,1066 656426 3934,5000 600 50,5050 660410 "
            "1994950:timeout=3600:rewrite-security-xattrs=true:writeback=true:"
            "negative_timeout=3600");
}

TEST(VMUtilTest, SharedDataParamCacheAlwaysCaseFold) {
  SharedDataParam param{
      .data_dir = base::FilePath("/run/arcvm/android-data"),
      .tag = "_data_media",
      .uid_map = kAndroidUidMap,
      .gid_map = kAndroidGidMap,
      .enable_caches = SharedDataParam::Cache::kAlways,
      .ascii_casefold = true,
      .posix_acl = true,
      .privileged_quota_uids = {0},
  };
  ASSERT_EQ(param.to_string(),
            "/run/arcvm/android-data:_data_media:type=fs:cache=always:uidmap=0 "
            "655360 5000,5000 600 50,5050 660410 1994950:gidmap=0 655360 "
            "1065,1065 20119 1,1066 656426 3934,5000 600 50,5050 660410 "
            "1994950:timeout=3600:rewrite-security-xattrs=true:ascii_casefold="
            "true:writeback=true:negative_timeout=0:privileged_quota_uids=0");
}

TEST(VMUtilTest, SharedDataParamCacheAuto) {
  SharedDataParam param{.data_dir = base::FilePath("/usr/local/bin"),
                        .tag = "usr_local_bin",
                        .uid_map = kAndroidUidMap,
                        .gid_map = kAndroidGidMap,
                        .enable_caches = SharedDataParam::Cache::kAuto,
                        .ascii_casefold = false,
                        .posix_acl = true};
  ASSERT_EQ(param.to_string(),
            "/usr/local/bin:usr_local_bin:type=fs:cache=auto:uidmap=0 655360 "
            "5000,5000 600 50,5050 660410 1994950:gidmap=0 655360 1065,1065 "
            "20119 1,1066 656426 3934,5000 600 50,5050 660410 "
            "1994950:timeout=1:rewrite-security-xattrs=true:writeback=false:"
            "negative_timeout=1");
}

TEST(VMUtilTest, SharedDataParamCacheNever) {
  SharedDataParam param{.data_dir = base::FilePath("/usr/local/bin"),
                        .tag = "usr_local_bin",
                        .uid_map = kAndroidUidMap,
                        .gid_map = kAndroidGidMap,
                        .enable_caches = SharedDataParam::Cache::kNever,
                        .ascii_casefold = false,
                        .posix_acl = true};
  ASSERT_EQ(param.to_string(),
            "/usr/local/bin:usr_local_bin:type=fs:cache=never:uidmap=0 655360 "
            "5000,5000 600 50,5050 660410 1994950:gidmap=0 655360 1065,1065 "
            "20119 1,1066 656426 3934,5000 600 50,5050 660410 "
            "1994950:timeout=1:rewrite-security-xattrs=true:writeback=false:"
            "negative_timeout=1");
}

// privileged_quota_uids is passed in.
TEST(VMUtilTest, SharedDataParamWithPrivilegedQuotaUids) {
  SharedDataParam param{.data_dir = base::FilePath("/usr/local/bin"),
                        .tag = "usr_local_bin",
                        .uid_map = kAndroidUidMap,
                        .gid_map = kAndroidGidMap,
                        .enable_caches = SharedDataParam::Cache::kAlways,
                        .ascii_casefold = false,
                        .posix_acl = true,
                        .privileged_quota_uids = {0}};
  ASSERT_EQ(param.to_string(),
            "/usr/local/bin:usr_local_bin:type=fs:cache=always:uidmap=0 655360 "
            "5000,5000 600 50,5050 660410 1994950:gidmap=0 655360 1065,1065 "
            "20119 1,1066 656426 3934,5000 600 50,5050 660410 "
            "1994950:timeout=3600:rewrite-security-xattrs=true:writeback=true:"
            "negative_timeout=3600:privileged_quota_uids=0");
}

TEST(VMUtilTest, GetBalloonStats) {
  FakeCrosvmControl::Init();
  FakeCrosvmControl::Get()->actual_balloon_size_ = 100;
  FakeCrosvmControl::Get()->balloon_stats_ = {
      .swap_in = 1,
      .swap_out = 2,
      .major_faults = 3,
      .minor_faults = 4,
      .free_memory = 5,
      .total_memory = 6,
      .available_memory = 7,
      .disk_caches = 8,
      .hugetlb_allocations = 9,
      .hugetlb_failures = 10,
      .shared_memory = 11,
      .unevictable_memory = 12,
  };

  std::optional<BalloonStats> stats =
      GetBalloonStats("/run/nothing", std::nullopt);

  ASSERT_TRUE(stats);
  ASSERT_EQ(stats->balloon_actual, 100);
  ASSERT_EQ(stats->stats_ffi.swap_in, 1);
  ASSERT_EQ(stats->stats_ffi.swap_out, 2);
  ASSERT_EQ(stats->stats_ffi.major_faults, 3);
  ASSERT_EQ(stats->stats_ffi.minor_faults, 4);
  ASSERT_EQ(stats->stats_ffi.free_memory, 5);
  ASSERT_EQ(stats->stats_ffi.total_memory, 6);
  ASSERT_EQ(stats->stats_ffi.available_memory, 7);
  ASSERT_EQ(stats->stats_ffi.disk_caches, 8);
  ASSERT_EQ(stats->stats_ffi.hugetlb_allocations, 9);
  ASSERT_EQ(stats->stats_ffi.hugetlb_failures, 10);
  ASSERT_EQ(stats->stats_ffi.shared_memory, 11);
  ASSERT_EQ(stats->stats_ffi.unevictable_memory, 12);
  ASSERT_EQ(FakeCrosvmControl::Get()->target_socket_path_, "/run/nothing");
  CrosvmControl::Reset();
}

// Exercise retrieval of WorkingSet from CrosvmControl wrapper.
TEST(VMUtilTest, GetBalloonWorkingSet) {
  FakeCrosvmControl::Init();
  FakeCrosvmControl::Get()->actual_balloon_size_ = 100;
  WorkingSetBucketFfi wsb1 = {
      .age = 100,
      .bytes = {10, 20},
  };
  WorkingSetBucketFfi wsb2 = {
      .age = 200,
      .bytes = {11, 21},
  };
  WorkingSetBucketFfi wsb3 = {
      .age = 300,
      .bytes = {12, 22},
  };
  WorkingSetBucketFfi wsb4 = {
      .age = 400,
      .bytes = {13, 23},
  };
  FakeCrosvmControl::Get()->balloon_working_set_ = {
      .ws = {wsb1, wsb2, wsb3, wsb4}};

  std::optional<BalloonWorkingSet> ws = GetBalloonWorkingSet("/run/nothing");
  ASSERT_TRUE(ws);

  // Test that the returned working set has the expected values in all fields.
  ASSERT_EQ(ws->balloon_actual, 100);
  ASSERT_EQ(ws->working_set_ffi.ws[0].age, 100);
  ASSERT_EQ(ws->working_set_ffi.ws[0].bytes[0], 10);
  ASSERT_EQ(ws->working_set_ffi.ws[0].bytes[1], 20);
  ASSERT_EQ(ws->working_set_ffi.ws[1].age, 200);
  ASSERT_EQ(ws->working_set_ffi.ws[1].bytes[0], 11);
  ASSERT_EQ(ws->working_set_ffi.ws[1].bytes[1], 21);
  ASSERT_EQ(ws->working_set_ffi.ws[2].age, 300);
  ASSERT_EQ(ws->working_set_ffi.ws[2].bytes[0], 12);
  ASSERT_EQ(ws->working_set_ffi.ws[2].bytes[1], 22);
  ASSERT_EQ(ws->working_set_ffi.ws[3].age, 400);
  ASSERT_EQ(ws->working_set_ffi.ws[3].bytes[0], 13);
  ASSERT_EQ(ws->working_set_ffi.ws[3].bytes[1], 23);
  CrosvmControl::Reset();
}

TEST(VMUtilTest, GetDevConfPath) {
  // It was "/usr/local/vms/etc/arcvm_dev.conf" before, and should stay that
  // way.
  EXPECT_EQ(internal::GetDevConfPath(apps::VmType::ARCVM),
            "/usr/local/vms/etc/arcvm_dev.conf");

  // Others look like this:
  EXPECT_EQ(internal::GetDevConfPath(apps::VmType::TERMINA),
            "/usr/local/vms/etc/termina_dev.conf");
  EXPECT_EQ(internal::GetDevConfPath(apps::VmType::BOREALIS),
            "/usr/local/vms/etc/borealis_dev.conf");
  EXPECT_EQ(internal::GetDevConfPath(apps::VmType::BRUSCHETTA),
            "/usr/local/vms/etc/bruschetta_dev.conf");
}

TEST(VMUtilTest, GetVmMemoryMiB) {
  // elm 4GB SKUs.
  EXPECT_EQ(internal::GetVmMemoryMiBInternal(3885, /* is_32bit */ true), 2913);

  // trogdor 4GB SKUs.
  EXPECT_EQ(internal::GetVmMemoryMiBInternal(3885, /* is_32bit */ false), 2913);

  // jacuzzi 8GB SKUs.
  EXPECT_EQ(internal::GetVmMemoryMiBInternal(7915, /* is_32bit */ true), 3328);

  // corsola 8GB SKUs.
  EXPECT_EQ(internal::GetVmMemoryMiBInternal(7915, /* is_32bit */ false), 6891);

  // 16GB Brya
  EXPECT_EQ(internal::GetVmMemoryMiBInternal(15785, /* is_32bit */ false),
            14761);
}

TEST(VMUtilTest, GetCpuPackageIdAndCapacity) {
  // Create temporary directory for CPU topology
  base::ScopedTempDir cpu_info_dir;
  EXPECT_TRUE(cpu_info_dir.CreateUniqueTempDir());
  EXPECT_TRUE(
      base::CreateDirectory(cpu_info_dir.GetPath().Append("cpu0/topology/")));

  // Create package_id file inside temporary directory
  base::FilePath cpu_id_path =
      cpu_info_dir.GetPath().Append("cpu0/topology/physical_package_id");
  base::File id_file(cpu_id_path, base::File::FLAG_CREATE |
                                      base::File::FLAG_WRITE |
                                      base::File::FLAG_READ);
  EXPECT_TRUE(id_file.created());

  // Test on artificial CPU with out corresponding physical cpu
  auto noneID = GetCpuPackageId(0, cpu_info_dir.GetPath());
  EXPECT_FALSE(noneID);

  // Test on valid cpu id
  constexpr char test_cpu_id[] = "1";
  base::WriteFile(cpu_id_path, test_cpu_id);
  auto validID = GetCpuPackageId(0, cpu_info_dir.GetPath());
  EXPECT_TRUE(validID);
  EXPECT_EQ(*validID, 1);

  // Create cpu_capacity file inside temporary directory
  base::FilePath cpu_capacity_path =
      cpu_info_dir.GetPath().Append("cpu0/cpu_capacity");
  base::File capacity_file(cpu_id_path, base::File::FLAG_CREATE |
                                            base::File::FLAG_WRITE |
                                            base::File::FLAG_READ);

  // Test on no cpu capacity infos
  auto noneCapacity = GetCpuCapacity(0, cpu_info_dir.GetPath());
  EXPECT_FALSE(noneCapacity);

  // Test on valid cpu capacity
  constexpr char test_cpu_capacity[] = "741";
  base::WriteFile(cpu_capacity_path, test_cpu_capacity);
  auto valid_capacity = GetCpuCapacity(0, cpu_info_dir.GetPath());
  EXPECT_TRUE(valid_capacity);
  EXPECT_EQ(*valid_capacity, 741);
}

TEST(VMUtilTest, VmStatusConversion) {
  ASSERT_EQ(ToVmStatus(VmBaseImpl::Status::STARTING), VM_STATUS_STARTING);
  ASSERT_EQ(ToVmStatus(VmBaseImpl::Status::RUNNING), VM_STATUS_RUNNING);
  ASSERT_EQ(ToVmStatus(VmBaseImpl::Status::STOPPED), VM_STATUS_STOPPED);
}

namespace {

const VmBaseImpl::Info test_info = {
    .ipv4_address = net_base::IPv4Address(127, 0, 0, 1).ToInAddr().s_addr,
    .pid = 123,
    .cid = 22,
    .seneschal_server_handle = 1,
    .permission_token = "secret token",
    .status = VmBaseImpl::Status::RUNNING,
    .type = apps::VmType::TERMINA,
    .storage_ballooning = true,
};

}  // namespace

TEST(VMUtilTest, VmInfoConversioniWithSensitive) {
  VmInfo vm_info = ToVmInfo(test_info, true);

  ASSERT_EQ(vm_info.ipv4_address(),
            net_base::IPv4Address(127, 0, 0, 1).ToInAddr().s_addr);
  ASSERT_EQ(vm_info.pid(), 123);
  ASSERT_EQ(vm_info.cid(), 22);
  ASSERT_EQ(vm_info.seneschal_server_handle(), 1);
  ASSERT_EQ(vm_info.permission_token(), "secret token");
  ASSERT_EQ(vm_info.vm_type(), VmInfo::TERMINA);
  ASSERT_TRUE(vm_info.storage_ballooning());
  ASSERT_EQ(vm_info.status(), VM_STATUS_RUNNING);
}

TEST(VMUtilTest, VmInfoConversionWithoutSensitive) {
  VmInfo vm_info = ToVmInfo(test_info, false);

  ASSERT_EQ(vm_info.ipv4_address(),
            net_base::IPv4Address(127, 0, 0, 1).ToInAddr().s_addr);
  ASSERT_EQ(vm_info.pid(), 123);
  ASSERT_EQ(vm_info.cid(), 22);
  ASSERT_EQ(vm_info.seneschal_server_handle(), 1);
  ASSERT_EQ(vm_info.permission_token(), "");
  ASSERT_EQ(vm_info.vm_type(), VmInfo::TERMINA);
  ASSERT_TRUE(vm_info.storage_ballooning());
  ASSERT_EQ(vm_info.status(), VM_STATUS_RUNNING);
}

}  // namespace concierge
}  // namespace vm_tools
