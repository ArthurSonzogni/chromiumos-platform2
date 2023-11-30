// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/json/json_reader.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "runtime_probe/functions/mmc_storage.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

constexpr auto kDebugdMmcOption = "extcsd_read";

class MockMmcStorageFunction : public MmcStorageFunction {
  using MmcStorageFunction::MmcStorageFunction;

 public:
  using MmcStorageFunction::ProbeFromStorageTool;
  using MmcStorageFunction::ProbeFromSysfs;
};

class MmcStorageFunctionTest : public BaseFunctionTest {
 protected:
  // Sets up a mmc host under /sys/devices.
  void SetMmcHost(
      const std::string& mmc_host_name,
      const std::string& bus_type,
      const std::vector<std::pair<std::string, std::string>>& mmc_host_fields) {
    // Sets up the bus device.
    for (const auto& [field, value] : mmc_host_fields) {
      SetFile({"sys", "devices", "bus_device", field}, value);
    }
    SetSymbolicLink({"..", "bus", bus_type},
                    {"sys", "devices", "bus_device", "subsystem"});
    // Sets up mmc_host device which points to the bus device.
    SetSymbolicLink({"..", ".."}, {"sys", "devices", "bus_device", "mmc_host",
                                   mmc_host_name, "device"});
  }

  // Sets up a mmc storage under /sys/devices/{mmc_host_name}.
  void SetMmcStorage(
      const std::string& mmc_host_name,
      const std::string& mmc_name,
      const std::vector<std::pair<std::string, std::string>>& mmc_fields) {
    SetSymbolicLink({"..", "..", "..", "devices", "bus_device", "mmc_host",
                     mmc_host_name, mmc_name},
                    {"sys", "class", "block", mmc_name, "device"});
    for (auto& [field, value] : mmc_fields) {
      SetFile({"sys", "devices", "bus_device", "mmc_host", mmc_host_name,
               mmc_name, field},
              value);
    }
  }
};

TEST_F(MmcStorageFunctionTest, ProbeFromSysfs) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  const std::string mmc_host_name = "mmc0";
  const std::string mmc_name = "mmcblk1";
  SetMmcHost(mmc_host_name, "platform", {});
  SetMmcStorage(mmc_host_name, mmc_name,
                {{"type", "MMC"},
                 {"name", "AB1234"},
                 {"oemid", "0x0001"},
                 {"manfid", "0x000002"}});

  auto result = probe_function->ProbeFromSysfs(
      GetPathUnderRoot({"sys", "class", "block", mmc_name}));
  auto ans = base::JSONReader::Read(R"JSON(
    {
      "mmc_host_bus_type": "uninterested",
      "mmc_name": "AB1234",
      "mmc_oemid": "0x0001",
      "mmc_manfid": "0x000002",
      "type": "MMC_ASSEMBLY"
    }
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(MmcStorageFunctionTest, ProbeFromSysfsPciHost) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  const std::string mmc_host_name = "mmc0";
  const std::string mmc_name = "mmcblk1";
  SetMmcHost(mmc_host_name, "pci",
             {{"vendor", "0x1111"},
              {"device", "0x2222"},
              {"class", "0x010203"},
              {"revision", "0x01"},
              {"subsystem_device", "0x3333"}});

  SetMmcStorage(mmc_host_name, mmc_name,
                {{"type", "MMC"},
                 {"name", "AB1234"},
                 {"oemid", "0x0001"},
                 {"manfid", "0x000002"}});

  auto result = probe_function->ProbeFromSysfs(
      GetPathUnderRoot({"sys", "class", "block", mmc_name}));
  auto ans = base::JSONReader::Read(R"JSON(
    {
      "mmc_host_bus_type": "pci",
      "mmc_host_pci_vendor_id" : "0x1111",
      "mmc_host_pci_device_id" : "0x2222",
      "mmc_host_pci_class" : "0x010203",
      "mmc_host_pci_revision" : "0x01",
      "mmc_host_pci_subsystem" : "0x3333",
      "mmc_name": "AB1234",
      "mmc_oemid": "0x0001",
      "mmc_manfid": "0x000002",
      "type": "MMC_ASSEMBLY"
    }
  )JSON");
  // We don't want to check the actual value of path.
  EXPECT_TRUE(result->GetDict().Remove("mmc_host_path"));
  EXPECT_EQ(result, ans);
}

TEST_F(MmcStorageFunctionTest, ProbeFromSysfsNonMmcStorage) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  const std::string mmc_host_name = "mmc0";
  const std::string mmc_name = "mmcblk1";
  SetMmcHost(mmc_host_name, "platform", {});
  // The type of the storage is "unknown", not MMC.
  SetMmcStorage(mmc_host_name, mmc_name,
                {{"type", "unknown"},
                 {"name", "AB1234"},
                 {"oemid", "0x0001"},
                 {"manfid", "0x000002"}});

  auto result = probe_function->ProbeFromSysfs(
      GetPathUnderRoot({"sys", "class", "block", mmc_name}));
  // The result should be std::nullopt for non-mmc storages.
  EXPECT_EQ(result, std::nullopt);
}

TEST_F(MmcStorageFunctionTest, ProbeFromSysfsNoTypeFile) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  const std::string mmc_host_name = "mmc0";
  const std::string mmc_name = "mmcblk1";
  SetMmcHost(mmc_host_name, "platform", {});
  // No file for mmc type.
  SetMmcStorage(
      mmc_host_name, mmc_name,
      {{"name", "AB1234"}, {"oemid", "0x0001"}, {"manfid", "0x000002"}});

  auto result = probe_function->ProbeFromSysfs(
      GetPathUnderRoot({"sys", "class", "block", mmc_name}));
  // The result should be std::nullopt for mmc without type.
  EXPECT_EQ(result, std::nullopt);
}

TEST_F(MmcStorageFunctionTest, ProbeFromSysfsNoRequiredFields) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  const std::string mmc_host_name = "mmc0";
  const std::string mmc_name = "mmcblk1";
  SetMmcHost(mmc_host_name, "platform", {});
  // No required field "name".
  SetMmcStorage(mmc_host_name, mmc_name,
                {{"type", "MMC"}, {"oemid", "0x0001"}, {"manfid", "0x000002"}});

  auto result = probe_function->ProbeFromSysfs(
      GetPathUnderRoot({"sys", "class", "block", mmc_name}));
  // The result should be std::nullopt for storages without required fields.
  EXPECT_EQ(result, std::nullopt);
}

TEST_F(MmcStorageFunctionTest, ProbeFromStorageToolWithAsciiStringFwVersion) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  std::string mmc_extcsd_output = R"(Firmware version:
[FIRMWARE_VERSION[261]]: 0x48
[FIRMWARE_VERSION[260]]: 0x47
[FIRMWARE_VERSION[259]]: 0x46
[FIRMWARE_VERSION[258]]: 0x45
[FIRMWARE_VERSION[257]]: 0x44
[FIRMWARE_VERSION[256]]: 0x43
[FIRMWARE_VERSION[255]]: 0x42
[FIRMWARE_VERSION[254]]: 0x41)";
  auto debugd = mock_context()->mock_debugd_proxy();
  EXPECT_CALL(*debugd, Mmc(kDebugdMmcOption, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(mmc_extcsd_output), Return(true)));

  auto result =
      probe_function->ProbeFromStorageTool(base::FilePath("/unused/path"));
  auto ans = base::JSONReader::Read(R"JSON(
    {
      "storage_fw_version": "4142434445464748 (ABCDEFGH)"
    }
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(MmcStorageFunctionTest, ProbeFromStorageToolWithHexValueFwVersion) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  std::string mmc_extcsd_output = R"(Firmware version:
[FIRMWARE_VERSION[261]]: 0x00
[FIRMWARE_VERSION[260]]: 0x00
[FIRMWARE_VERSION[259]]: 0x00
[FIRMWARE_VERSION[258]]: 0x00
[FIRMWARE_VERSION[257]]: 0x00
[FIRMWARE_VERSION[256]]: 0x00
[FIRMWARE_VERSION[255]]: 0x00
[FIRMWARE_VERSION[254]]: 0x03)";
  auto debugd = mock_context()->mock_debugd_proxy();
  EXPECT_CALL(*debugd, Mmc(kDebugdMmcOption, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(mmc_extcsd_output), Return(true)));

  auto result =
      probe_function->ProbeFromStorageTool(base::FilePath("/unused/path"));
  auto ans = base::JSONReader::Read(R"JSON(
    {
      "storage_fw_version": "0300000000000000 (3)"
    }
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(MmcStorageFunctionTest, ProbeFromStorageToolInvalidFwVersionHexValue) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  // Invalid hex representation 0xZZ.
  std::string invalid_mmc_extcsd_output = R"(Firmware version:
[FIRMWARE_VERSION[261]]: 0xZZ
[FIRMWARE_VERSION[260]]: 0x00
[FIRMWARE_VERSION[259]]: 0x00
[FIRMWARE_VERSION[258]]: 0x00
[FIRMWARE_VERSION[257]]: 0x00
[FIRMWARE_VERSION[256]]: 0x00
[FIRMWARE_VERSION[255]]: 0x00
[FIRMWARE_VERSION[254]]: 0x03)";
  auto debugd = mock_context()->mock_debugd_proxy();
  EXPECT_CALL(*debugd, Mmc(kDebugdMmcOption, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(invalid_mmc_extcsd_output), Return(true)));

  auto result =
      probe_function->ProbeFromStorageTool(base::FilePath("/unused/path"));
  // Failed to get the firmware version. Field storage_fw_version should not be
  // probed.
  auto ans = base::JSONReader::Read(R"JSON(
    {}
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(MmcStorageFunctionTest, ProbeFromStorageToolInvalidFwVersionByteCount) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  // The output for firmware version should be 8 bytes, but got only 1 byte.
  std::string invalid_mmc_extcsd_output = R"(Firmware version:
[FIRMWARE_VERSION[261]]: 0x03)";
  auto debugd = mock_context()->mock_debugd_proxy();
  EXPECT_CALL(*debugd, Mmc(kDebugdMmcOption, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(invalid_mmc_extcsd_output), Return(true)));

  auto result =
      probe_function->ProbeFromStorageTool(base::FilePath("/unused/path"));
  // Failed to get the firmware version. Field storage_fw_version should not be
  // probed.
  auto ans = base::JSONReader::Read(R"JSON(
    {}
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(MmcStorageFunctionTest, ProbeFromStorageToolDBusCallFailed) {
  auto probe_function = CreateProbeFunction<MockMmcStorageFunction>();

  auto debugd = mock_context()->mock_debugd_proxy();
  // D-Bus call to debugd failed.
  EXPECT_CALL(*debugd, Mmc(kDebugdMmcOption, _, _, _))
      .WillRepeatedly(Return(false));

  auto result =
      probe_function->ProbeFromStorageTool(base::FilePath("/unused/path"));
  // Failed to get the firmware version. Field storage_fw_version should not be
  // probed.
  auto ans = base::JSONReader::Read(R"JSON(
    {}
  )JSON");
  EXPECT_EQ(result, ans);
}

}  // namespace
}  // namespace runtime_probe
