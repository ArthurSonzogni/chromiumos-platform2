// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include <gtest/gtest.h>

#include "runtime_probe/functions/gpu.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

// using ::testing::UnorderedElementsAreArray;

class GpuFunctionTest : public BaseFunctionTest {
 protected:
  void SetPciDevice(const std::string& pci_device_id,
                    const std::map<std::string, std::string> files) {
    SetSymbolicLink({"../../../devices/pci0000:00/0000:00:08.1", pci_device_id},
                    {"sys/bus/pci/devices", pci_device_id});
    for (const auto& file : files) {
      SetFile(
          {"sys/devices/pci0000:00/0000:00:08.1", pci_device_id, file.first},
          file.second);
    }
  }
};

TEST_F(GpuFunctionTest, ProbeGpu) {
  base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function = CreateProbeFunction<GpuFunction>(probe_statement);
  SetPciDevice("0000:04:00.0", {
                                   {"class", "0x030000"},
                                   {"vendor", "0x1234"},
                                   {"device", "0x5678"},
                                   {"subsystem_vendor", "0x90ab"},
                                   {"subsystem_device", "0xcdef"},
                               });
  // Class code 0x300001(ProgIf is 0x01) should be probed.
  SetPciDevice("0000:08:00.0", {
                                   {"class", "0x030001"},
                                   {"vendor", "0x1234"},
                                   {"device", "0x5678"},
                                   {"subsystem_vendor", "0x90ab"},
                                   {"subsystem_device", "0xcdef"},
                               });

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "vendor": "0x1234",
        "device": "0x5678",
        "subsystem_vendor": "0x90ab",
        "subsystem_device": "0xcdef"
      },
      {
        "vendor": "0x1234",
        "device": "0x5678",
        "subsystem_vendor": "0x90ab",
        "subsystem_device": "0xcdef"
      }
    ]
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(GpuFunctionTest, NonGpu) {
  base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function = CreateProbeFunction<GpuFunction>(probe_statement);
  // Non-display controller (class it not 0x30).
  SetPciDevice("0000:04:00.0", {
                                   {"class", "0x020000"},
                                   {"vendor", "0x1234"},
                                   {"device", "0x5678"},
                                   {"subsystem_vendor", "0x90ab"},
                                   {"subsystem_device", "0xcdef"},
                               });
  // Class code 0x038000(Subclass is 0x80) should not be probed.
  SetPciDevice("0000:08:00.0", {
                                   {"class", "0x038000"},
                                   {"vendor", "0x1234"},
                                   {"device", "0x5678"},
                                   {"subsystem_vendor", "0x90ab"},
                                   {"subsystem_device", "0xcdef"},
                               });

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(GpuFunctionTest, MissField) {
  base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function = CreateProbeFunction<GpuFunction>(probe_statement);
  // Each of these miss one field so won't be probed.
  SetPciDevice("0000:04:00.0", {
                                   {"class", "0x030000"},
                                   {"device", "0x5678"},
                                   {"subsystem_vendor", "0x90ab"},
                                   {"subsystem_device", "0xcdef"},
                               });
  SetPciDevice("0000:04:00.1", {
                                   {"class", "0x030000"},
                                   {"vendor", "0x1234"},
                                   {"subsystem_vendor", "0x90ab"},
                                   {"subsystem_device", "0xcdef"},
                               });
  SetPciDevice("0000:04:00.2", {
                                   {"class", "0x030000"},
                                   {"vendor", "0x1234"},
                                   {"device", "0x5678"},
                                   {"subsystem_device", "0xcdef"},
                               });
  SetPciDevice("0000:04:00.3", {
                                   {"class", "0x030000"},
                                   {"vendor", "0x1234"},
                                   {"device", "0x5678"},
                                   {"subsystem_vendor", "0x90ab"},
                               });

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

}  // namespace
}  // namespace runtime_probe
