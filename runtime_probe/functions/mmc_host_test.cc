// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gtest/gtest.h>

#include "runtime_probe/functions/mmc_host.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

constexpr char kSysClassMmcHostDir[] = "/sys/class/mmc_host";

class MmcHostFunctionTest : public BaseFunctionTest {
 protected:
  void SetMmcHost() {
    const std::string dev_name = "mmc0";
    const std::string bus_dev = "/sys/devices/pci0000:00/0000:00:08.1";
    const std::string bus_dev_relative_to_sys = "../../../";
    SetSymbolicLink(bus_dev, {kSysClassMmcHostDir, dev_name, "device"});
    // The symbolic link is for getting the bus type.
    SetSymbolicLink({bus_dev_relative_to_sys, "bus", "pci"},
                    {bus_dev, "subsystem"});
    SetFile({bus_dev, "device"}, "0x1111");
    SetFile({bus_dev, "vendor"}, "0x2222");
  }
};

TEST_F(MmcHostFunctionTest, ProbeMmcHost) {
  SetMmcHost();
  base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function = CreateProbeFunction<MmcHostFunction>(probe_statement);

  auto result = probe_function->Eval();
  EXPECT_EQ(result.size(), 1);
  EXPECT_TRUE(result[0].GetDict().FindString("path"));
  EXPECT_TRUE(result[0].GetDict().FindString("bus_type"));
}

}  // namespace
}  // namespace runtime_probe
