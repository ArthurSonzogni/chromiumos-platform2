// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/guid.h>
#include <base/logging.h>
#include <base/macros.h>
#include <gtest/gtest.h>

#include "vm_tools/cicerone/virtual_machine.h"

namespace vm_tools {
namespace cicerone {
namespace {

// Fake IP addresses to use for testing.
constexpr char kFakeIp1[] = "1.2.3.4";
constexpr char kFakeIp2[] = "5.6.7.8";

// Fake garcon vsock ports to use for testing.
constexpr uint32_t kFakeGarconPort1 = 1234;
constexpr uint32_t kFakeGarconPort2 = 2345;

// Fake container names to use for testing.
constexpr char kFakeContainerName1[] = "box";
constexpr char kFakeContainerName2[] = "cube";

}  // namespace

// Test fixture for actually testing the VirtualMachine functionality.
class VirtualMachineTest : public ::testing::Test {
 public:
  VirtualMachineTest() : vm_(0, 0, 0, 0) {}
  ~VirtualMachineTest() override = default;

 protected:
  // Actual virtual machine being tested.
  VirtualMachine vm_;

  DISALLOW_COPY_AND_ASSIGN(VirtualMachineTest);
};

TEST_F(VirtualMachineTest, NoContainerToken) {
  // If the token was never generated, then [un]registration should fail.
  EXPECT_FALSE(
      vm_.RegisterContainer(base::GenerateGUID(), kFakeGarconPort1, kFakeIp1));
  EXPECT_FALSE(vm_.UnregisterContainer(base::GenerateGUID()));
}

TEST_F(VirtualMachineTest, InvalidContainerToken) {
  // If the wrong token is used, then registration should fail.
  std::string token = vm_.GenerateContainerToken(kFakeContainerName1);
  EXPECT_FALSE(
      vm_.RegisterContainer(base::GenerateGUID(), kFakeGarconPort1, kFakeIp1));
  // Invalid token should fail unregister operation.
  EXPECT_FALSE(vm_.UnregisterContainer(base::GenerateGUID()));
}

TEST_F(VirtualMachineTest, ValidContainerToken) {
  // Valid process for generating a token and then registering it and
  // unregistering it.
  std::string token = vm_.GenerateContainerToken(kFakeContainerName1);
  EXPECT_TRUE(vm_.RegisterContainer(token, kFakeGarconPort1, kFakeIp1));
  EXPECT_EQ(kFakeContainerName1, vm_.GetContainerNameForToken(token));
  EXPECT_TRUE(vm_.UnregisterContainer(token));
  EXPECT_EQ("", vm_.GetContainerNameForToken(token));
}

TEST_F(VirtualMachineTest, ReuseContainerToken) {
  // Re-registering the same token is valid and unregistering it should work.
  std::string token = vm_.GenerateContainerToken(kFakeContainerName1);
  EXPECT_TRUE(vm_.RegisterContainer(token, kFakeGarconPort1, kFakeIp1));
  EXPECT_TRUE(vm_.RegisterContainer(token, kFakeGarconPort2, kFakeIp2));
  EXPECT_EQ(kFakeContainerName1, vm_.GetContainerNameForToken(token));
  EXPECT_TRUE(vm_.UnregisterContainer(token));
  EXPECT_EQ("", vm_.GetContainerNameForToken(token));
}

TEST_F(VirtualMachineTest, MultipleContainerTokens) {
  // Valid process for generating a token and then registering it from multiple
  // containers and also unregistering them.
  std::string token1 = vm_.GenerateContainerToken(kFakeContainerName1);
  EXPECT_TRUE(vm_.RegisterContainer(token1, kFakeGarconPort1, kFakeIp1));
  std::string token2 = vm_.GenerateContainerToken(kFakeContainerName2);
  EXPECT_TRUE(vm_.RegisterContainer(token2, kFakeGarconPort2, kFakeIp2));
  EXPECT_EQ(kFakeContainerName1, vm_.GetContainerNameForToken(token1));
  EXPECT_EQ(kFakeContainerName2, vm_.GetContainerNameForToken(token2));

  // Now unregister the first one.
  EXPECT_TRUE(vm_.UnregisterContainer(token1));
  EXPECT_EQ("", vm_.GetContainerNameForToken(token1));

  // Second one should still be there.
  EXPECT_EQ(kFakeContainerName2, vm_.GetContainerNameForToken(token2));

  // No unregister the second one.
  EXPECT_TRUE(vm_.UnregisterContainer(token2));
  EXPECT_EQ("", vm_.GetContainerNameForToken(token2));
}

}  // namespace cicerone
}  // namespace vm_tools
