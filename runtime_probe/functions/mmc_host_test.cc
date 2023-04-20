// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/functions/mmc_host.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

using ::testing::Eq;
using ::testing::Pointee;

constexpr char kSysClassMmcHostDir[] = "/sys/class/mmc_host";
constexpr char kFakeMmcName[] = "mmcX:1234";

class MmcHostFunctionTest : public BaseFunctionTest {
 protected:
  void SetUp() {
    base::Value probe_statement(base::Value::Type::DICT);
    probe_function_ = CreateProbeFunction<MmcHostFunction>(probe_statement);
  }

  // Returns the string of the real path of the fake mmc host device.
  std::string SetMmcHost() {
    const std::string dev_name = "mmc0";
    const std::string bus_dev = "/sys/devices/pci0000:00/0000:00:08.1";
    const std::string bus_dev_relative_to_sys = "../../../";
    SetSymbolicLink(bus_dev, {kSysClassMmcHostDir, dev_name, "device"});
    // The symbolic link is for getting the bus type.
    SetSymbolicLink({bus_dev_relative_to_sys, "bus", "pci"},
                    {bus_dev, "subsystem"});
    SetFile({bus_dev, "device"}, "0x1111");
    SetFile({bus_dev, "vendor"}, "0x2222");

    return bus_dev + "/mmc_host/" + dev_name;
  }

  std::unique_ptr<ProbeFunction> probe_function_;
};

TEST_F(MmcHostFunctionTest, ProbeMmcHost) {
  SetMmcHost();

  auto result = probe_function_->Eval();
  EXPECT_EQ(result.size(), 1);
  EXPECT_TRUE(result[0].GetDict().FindString("path"));
  EXPECT_TRUE(result[0].GetDict().FindString("bus_type"));
}

TEST_F(MmcHostFunctionTest, NoMmcDeviceAttached) {
  SetMmcHost();

  auto result = probe_function_->Eval();
  EXPECT_EQ(result.size(), 1);
  EXPECT_THAT(result[0].GetDict().FindString("is_emmc_attached"),
              Pointee(Eq("0")));
}

TEST_F(MmcHostFunctionTest, EmmcDeviceAttached) {
  const std::string mmc_host_dev = SetMmcHost();
  SetSymbolicLink({mmc_host_dev, kFakeMmcName},
                  {"/sys/bus/mmc/devices", kFakeMmcName});
  SetFile({mmc_host_dev, kFakeMmcName, "type"}, "MMC");

  auto result = probe_function_->Eval();
  EXPECT_EQ(result.size(), 1);
  EXPECT_THAT(result[0].GetDict().FindString("is_emmc_attached"),
              Pointee(Eq("1")));
}

TEST_F(MmcHostFunctionTest, SDCardDeviceAttached) {
  const std::string mmc_host_dev = SetMmcHost();
  SetSymbolicLink({mmc_host_dev, kFakeMmcName},
                  {"/sys/bus/mmc/devices", kFakeMmcName});
  SetFile({mmc_host_dev, kFakeMmcName, "type"}, "SD");

  auto result = probe_function_->Eval();
  EXPECT_EQ(result.size(), 1);
  EXPECT_THAT(result[0].GetDict().FindString("is_emmc_attached"),
              Pointee(Eq("0")));
}

TEST_F(MmcHostFunctionTest, UnknownDeviceAttached) {
  const std::string mmc_host_dev = SetMmcHost();
  SetSymbolicLink({mmc_host_dev, kFakeMmcName},
                  {"/sys/bus/mmc/devices", kFakeMmcName});
  UnsetPath({mmc_host_dev, kFakeMmcName, "type"});

  auto result = probe_function_->Eval();
  EXPECT_EQ(result.size(), 1);
  EXPECT_THAT(result[0].GetDict().FindString("is_emmc_attached"),
              Pointee(Eq("0")));
}

}  // namespace
}  // namespace runtime_probe
