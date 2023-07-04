// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_builder.h"

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vm_tools::concierge {

TEST(VmBuilderTest, DefaultValuesSucceeds) {
  VmBuilder builder;
  EXPECT_FALSE(std::move(builder).BuildVmArgs()->empty());
}

TEST(VmBuilderTest, CustomParametersWithCrosvmFlags) {
  CustomParametersForDev dev{R"(prerun:--log-level=debug)"};

  VmBuilder builder;
  auto result = std::move(builder).BuildVmArgs(&dev).value();

  EXPECT_EQ(result[0].first, "/usr/bin/crosvm");
  EXPECT_EQ(result[1].first, "--log-level");
  EXPECT_EQ(result[1].second, "debug");
  EXPECT_EQ(result[2].first, "run");
}

TEST(VmBuilderTest, CustomParametersWithStrace) {
  CustomParametersForDev dev{
      R"(precrosvm:/usr/local/bin/strace
precrosvm:-f
precrosvm:-o=/run/vm/crosvm_strace)"};

  VmBuilder builder;
  auto result = std::move(builder).BuildVmArgs(&dev).value();
  EXPECT_EQ(result[0].first, "/usr/local/bin/strace");
  EXPECT_EQ(result[1].first, "-f");
  EXPECT_EQ(result[1].second, "");
  // We can't do preprocessing on the precrosvm arguments, so let it just pass
  // through.
  EXPECT_EQ(result[2].first, "-o=/run/vm/crosvm_strace");
  EXPECT_EQ(result[2].second, "");
  EXPECT_EQ(result[3].first, "/usr/bin/crosvm");
  EXPECT_EQ(result[4].first, "run");
}

TEST(VmBuilderTest, ODirectN) {
  CustomParametersForDev dev{R"(O_DIRECT_N=2)"};

  VmBuilder builder;
  builder.AppendDisks(std::vector<Disk>{
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
  });
  auto result = std::move(builder).BuildVmArgs(&dev).value();

  std::vector<std::string> disk_params;
  for (auto& p : result) {
    if (p.first == "--disk") {
      disk_params.push_back(p.second);
    }
  }

  EXPECT_EQ(disk_params[0], "/dev/zero");
  EXPECT_EQ(disk_params[1], "/dev/zero");
  EXPECT_EQ(disk_params[2], "/dev/zero,o_direct=true,block_size=4096");
}

TEST(VmBuilderTest, ODirectNs) {
  CustomParametersForDev dev{R"(O_DIRECT_N=1
O_DIRECT_N=2)"};

  VmBuilder builder;
  builder.AppendDisks(std::vector<Disk>{
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
  });
  auto result = std::move(builder).BuildVmArgs(&dev).value();

  std::vector<std::string> disk_params;
  for (auto& p : result) {
    if (p.first == "--disk") {
      disk_params.push_back(p.second);
    }
  }

  EXPECT_EQ(disk_params[0], "/dev/zero");
  EXPECT_EQ(disk_params[1], "/dev/zero,o_direct=true,block_size=4096");
  EXPECT_EQ(disk_params[2], "/dev/zero,o_direct=true,block_size=4096");
}

TEST(VmBuilderTest, ODirectTooLargeNDeath) {
  CustomParametersForDev dev{R"(O_DIRECT_N=15)"};
  VmBuilder builder;
  ASSERT_DEATH({ auto result = std::move(builder).BuildVmArgs(&dev); },
               "out_of_range");
}

TEST(VmBuilderTest, DefaultKernel) {
  VmBuilder builder;
  builder.SetKernel(base::FilePath("/dev/null"));
  auto result = std::move(builder).BuildVmArgs().value();

  EXPECT_EQ(result[result.size() - 1].first, "/dev/null");
}

TEST(VmBuilderTest, CustomKernel) {
  CustomParametersForDev dev{R"(KERNEL_PATH=/dev/zero)"};

  VmBuilder builder;
  builder.SetKernel(base::FilePath("/dev/null"));
  auto result = std::move(builder).BuildVmArgs(&dev).value();

  EXPECT_EQ(result[result.size() - 1].first, "/dev/zero");
}

}  // namespace vm_tools::concierge
