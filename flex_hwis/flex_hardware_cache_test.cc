// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hardware_cache.h"
#include "flex_hwis/flex_hwis_mojo.h"
#include "flex_hwis/telemetry_for_testing.h"

#include <string>
#include <vector>

#include <chromeos/constants/flex_hwis.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace flex_hwis {

hwis_proto::Device Data() {
  flex_hwis::TelemetryForTesting telemetry;
  telemetry.AddSystemInfo();
  telemetry.AddCpuInfo();
  telemetry.AddMemoryInfo();
  telemetry.AddPciBusInfo(mojom::BusDeviceClass::kEthernetController);
  telemetry.AddPciBusInfo(mojom::BusDeviceClass::kWirelessController);
  telemetry.AddPciBusInfo(mojom::BusDeviceClass::kBluetoothAdapter);
  telemetry.AddPciBusInfo(mojom::BusDeviceClass::kDisplayController);
  telemetry.AddGraphicsInfo();
  telemetry.AddInputInfo();
  telemetry.AddTpmInfo();

  flex_hwis::FlexHwisMojo flex_hwis_mojo;
  flex_hwis_mojo.SetTelemetryInfoForTesting(telemetry.Get());

  hwis_proto::Device data;
  flex_hwis_mojo.SetHwisInfo(&data);
  return data;
}

class FlexHardwareCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(test_dir_.CreateUniqueTempDir());

    root_ = test_dir_.GetPath();
    cache_dir_ = root_.Append(kFlexHardwareCacheDir);
    ASSERT_TRUE(base::CreateDirectory(cache_dir_));
  }

  void CheckFile(const base::FilePath& path,
                 const std::string& expected_contents) {
    std::string actual_contents;
    EXPECT_TRUE(base::ReadFileToString(path, &actual_contents));
    EXPECT_EQ(actual_contents, expected_contents);
  }

  void CheckFileSubstrings(const base::FilePath& path,
                           const std::vector<std::string> expected_substrings) {
    std::string actual_contents;
    EXPECT_TRUE(base::ReadFileToString(path, &actual_contents));
    for (auto& substr : expected_substrings) {
      EXPECT_THAT(actual_contents, testing::HasSubstr(substr));
    }
  }

  base::ScopedTempDir test_dir_;
  base::FilePath root_;
  base::FilePath cache_dir_;
};

// Check that all data makes the round trip correctly, in the right files.
TEST_F(FlexHardwareCacheTest, TestRoundTrip) {
  hwis_proto::Device data = Data();

  bool all_succeeded = WriteCacheToDisk(data, root_);

  ASSERT_TRUE(all_succeeded);

  CheckFile(cache_dir_.Append(kFlexProductNameKey), kSystemProductName);
  CheckFile(cache_dir_.Append(kFlexProductVendorKey), kSystemVersion);
  CheckFile(cache_dir_.Append(kFlexProductVersionKey), kSystemProductVersion);

  CheckFile(cache_dir_.Append(kFlexTotalMemoryKey), kMemoryKibStr);

  CheckFile(cache_dir_.Append(kFlexBiosVersionKey), kSystemBiosVersion);
  CheckFile(cache_dir_.Append(kFlexSecurebootKey), kSystemSecurebootStr);
  CheckFile(cache_dir_.Append(kFlexUefiKey), kSystemUefiStr);

  CheckFile(cache_dir_.Append(kFlexCpuNameKey), kCpuModelName);

  CheckFile(cache_dir_.Append(kFlexBluetoothDriverKey), kPciBusDriver);
  CheckFile(cache_dir_.Append(kFlexBluetoothIdKey), kPciId);
  CheckFile(cache_dir_.Append(kFlexBluetoothNameKey), kBusPciName);
  CheckFile(cache_dir_.Append(kFlexEthernetDriverKey), kPciBusDriver);
  CheckFile(cache_dir_.Append(kFlexEthernetIdKey), kPciId);
  CheckFile(cache_dir_.Append(kFlexEthernetNameKey), kBusPciName);
  CheckFile(cache_dir_.Append(kFlexWirelessDriverKey), kPciBusDriver);
  CheckFile(cache_dir_.Append(kFlexWirelessIdKey), kPciId);
  CheckFile(cache_dir_.Append(kFlexWirelessNameKey), kBusPciName);

  CheckFile(cache_dir_.Append(kFlexGpuDriverKey), kPciBusDriver);
  CheckFile(cache_dir_.Append(kFlexGpuIdKey), kPciId);
  CheckFile(cache_dir_.Append(kFlexGpuNameKey), kBusPciName);

  CheckFile(cache_dir_.Append(kFlexGlVersionKey), kGraphicsVersion);
  CheckFile(cache_dir_.Append(kFlexGlShadingVersionKey), kGraphicsShadingVer);
  CheckFile(cache_dir_.Append(kFlexGlVendorKey), kGraphicsVendor);
  CheckFile(cache_dir_.Append(kFlexGlRendererKey), kGraphicsRenderer);
  CheckFileSubstrings(
      cache_dir_.Append(kFlexGlExtensionsKey),
      {kGraphicsExtension1, kGraphicsExtension2, kGraphicsExtension3});

  CheckFile(cache_dir_.Append(kFlexTpmVersionKey), kTpmFamilyStr);
  CheckFile(cache_dir_.Append(kFlexTpmSpecLevelKey), kTpmSpecLevelStr);
  CheckFile(cache_dir_.Append(kFlexTpmManufacturerKey), kTpmManufacturerStr);
  CheckFile(cache_dir_.Append(kFlexTpmDidVidKey), kTpmDidVid);
  CheckFile(cache_dir_.Append(kFlexTpmAllowListedKey), kTpmIsAllowedStr);
  CheckFile(cache_dir_.Append(kFlexTpmOwnedKey), kTpmOwnedStr);

  CheckFile(cache_dir_.Append(kFlexTouchpadStackKey), kTouchpadLibraryName);
}

TEST_F(FlexHardwareCacheTest, TestMultipleDevicesMultipleDrivers) {
  flex_hwis::TelemetryForTesting telemetry;

  telemetry.AddUsbBusInfo(mojom::BusDeviceClass::kBluetoothAdapter, "vendor 1",
                          "product 1",
                          0x1234,  // vid
                          0x4321,  // pid
                          {"btusb", "btusb"});
  telemetry.AddUsbBusInfo(mojom::BusDeviceClass::kBluetoothAdapter, "vendor 2",
                          "product 2",
                          0x5678,  // vid
                          0x8765,  // pid
                          {"fake"});

  flex_hwis::FlexHwisMojo flex_hwis_mojo;
  flex_hwis_mojo.SetTelemetryInfoForTesting(telemetry.Get());

  hwis_proto::Device data;
  flex_hwis_mojo.SetHwisInfo(&data);

  bool all_succeeded = WriteCacheToDisk(data, root_);

  ASSERT_TRUE(all_succeeded);

  // Check that both id pairs are present (in either order) and separated by a
  // comma+space, to match what rubber-chicken shows.
  CheckFileSubstrings(cache_dir_.Append(kFlexBluetoothIdKey),
                      {"1234:4321", "5678:8765", ", "});

  // While the shape of the data allows this we've never seen it in practice,
  // only `btusb/btusb`.
  CheckFileSubstrings(cache_dir_.Append(kFlexBluetoothDriverKey),
                      {"btusb/btusb", "fake", ", "});
}

}  // namespace flex_hwis
