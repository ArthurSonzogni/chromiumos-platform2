// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/fwupd_utils.h"

#include <optional>

#include <base/strings/stringprintf.h>
#include <brillo/variant_dictionary.h>
#include <gtest/gtest.h>
#include <libfwupd/fwupd-enums.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics::fwupd_utils {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

TEST(FwupdUtilsTest, DeviceContainsVendorId) {
  auto device_info = DeviceInfo();
  device_info.joined_vendor_id = "USB:0x1234|PCI:0x5678";

  EXPECT_TRUE(ContainsVendorId(device_info, "USB:0x1234"));
  EXPECT_TRUE(ContainsVendorId(device_info, "PCI:0x5678"));

  EXPECT_FALSE(ContainsVendorId(device_info, "USB:0x4321"));
}

TEST(FwupdUtilsTest, DeviceContainsVendorIdWrongFormat) {
  auto device_info = DeviceInfo();
  device_info.joined_vendor_id = "USB:0x1234|PCI:0x5678";

  EXPECT_FALSE(ContainsVendorId(device_info, "1234"));
}

TEST(FwupdUtilsTest, DeviceContainsVendorIdNull) {
  auto device_info = DeviceInfo();

  EXPECT_FALSE(ContainsVendorId(device_info, "USB:0x1234"));
  EXPECT_FALSE(ContainsVendorId(device_info, ""));
}

TEST(FwupdUtilsTest, InstanceIdToGuid) {
  EXPECT_EQ(InstanceIdToGuid("USB\\VID_0A5C&PID_6412&REV_0001"),
            "52fd36dc-5904-5936-b114-d98e9d410b25");
  EXPECT_EQ(InstanceIdToGuid("USB\\VID_0A5C&PID_6412"),
            "7a1ba7b9-6bcd-54a4-8a36-d60cc5ee935c");
  EXPECT_EQ(InstanceIdToGuid("USB\\VID_0A5C"),
            "ddfc8e56-df0d-582e-af12-c7fa171233dc");
}

TEST(FwupdUtilsTest, InstanceIdToGuidConversionFails) {
  EXPECT_EQ(InstanceIdToGuid(""), std::nullopt);
}

TEST(FwupdUtilsTest, ParseDbusFwupdDeviceInfo) {
  std::vector<std::string> guids{"GUID_1", "GUID_2"};
  std::vector<std::string> instance_ids{"INSTNACE_ID1", "INSTNACE_ID2"};
  std::string serial("serial");
  std::string version("version");
  std::string joined_vendor_id("USB:0x1234|PCI:0x5678");

  brillo::VariantDictionary dbus_response_entry;
  dbus_response_entry.emplace(kFwupdReusltKeyGuid, guids);
  dbus_response_entry.emplace(kFwupdResultKeyInstanceIds, instance_ids);
  dbus_response_entry.emplace(kFwupdResultKeySerial, serial);
  dbus_response_entry.emplace(kFwupdResultKeyVendorId, joined_vendor_id);
  dbus_response_entry.emplace(kFwupdResultKeyVersion, version);
  dbus_response_entry.emplace(
      kFwupdResultKeyVersionFormat,
      static_cast<uint32_t>(FWUPD_VERSION_FORMAT_PLAIN));

  DeviceInfo device_info = ParseDbusFwupdDeviceInfo(dbus_response_entry);

  EXPECT_EQ(device_info.guids, guids);
  EXPECT_EQ(device_info.instance_ids, instance_ids);
  EXPECT_EQ(device_info.serial, serial);
  EXPECT_EQ(device_info.version, version);
  EXPECT_EQ(device_info.version_format, mojom::FwupdVersionFormat::kPlain);
  EXPECT_EQ(device_info.joined_vendor_id, joined_vendor_id);
}

TEST(FwupdUtilsTest, ParseDbusFwupdDeviceInfoMissingKeys) {
  // For std::vector<std::string>, fill guids but not instance_ids.
  // For std::optional<std::string>, fill product_name but not serial.
  std::vector<std::string> guids{"GUID_1", "GUID_2"};

  brillo::VariantDictionary dbus_response_entry;
  dbus_response_entry.emplace(kFwupdReusltKeyGuid, guids);

  DeviceInfo device_info = ParseDbusFwupdDeviceInfo(dbus_response_entry);

  EXPECT_EQ(device_info.guids, guids);
  EXPECT_EQ(device_info.instance_ids.size(), 0);
  EXPECT_EQ(device_info.serial, std::nullopt);
  EXPECT_EQ(device_info.version, std::nullopt);
  EXPECT_EQ(device_info.version_format, mojom::FwupdVersionFormat::kUnknown);
  EXPECT_EQ(device_info.joined_vendor_id, std::nullopt);
}

TEST(FwupdUtilsTest, ParseVersionFormat) {
  auto parse_helper = [](FwupdVersionFormat dbus_value) {
    brillo::VariantDictionary dbus_response_entry;
    dbus_response_entry.emplace(kFwupdResultKeyVersionFormat,
                                static_cast<uint32_t>(dbus_value));
    return ParseDbusFwupdDeviceInfo(dbus_response_entry).version_format;
  };

  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_PLAIN),
            mojom::FwupdVersionFormat::kPlain);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_NUMBER),
            mojom::FwupdVersionFormat::kNumber);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_PAIR),
            mojom::FwupdVersionFormat::kPair);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_TRIPLET),
            mojom::FwupdVersionFormat::kTriplet);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_QUAD),
            mojom::FwupdVersionFormat::kQuad);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_BCD),
            mojom::FwupdVersionFormat::kBcd);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_INTEL_ME),
            mojom::FwupdVersionFormat::kIntelMe);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_INTEL_ME2),
            mojom::FwupdVersionFormat::kIntelMe2);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_SURFACE_LEGACY),
            mojom::FwupdVersionFormat::kSurfaceLegacy);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_SURFACE),
            mojom::FwupdVersionFormat::kSurface);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_DELL_BIOS),
            mojom::FwupdVersionFormat::kDellBios);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_HEX),
            mojom::FwupdVersionFormat::kHex);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_LAST),
            mojom::FwupdVersionFormat::kUnknown);
  EXPECT_EQ(parse_helper(FWUPD_VERSION_FORMAT_UNKNOWN),
            mojom::FwupdVersionFormat::kUnknown);
  // Test for unexpected values since the values are from external D-Bus
  // services.
  EXPECT_EQ(parse_helper(
                static_cast<FwupdVersionFormat>(FWUPD_VERSION_FORMAT_LAST + 1)),
            mojom::FwupdVersionFormat::kUnknown);
}

TEST(FwupdUtilsTest, MatchUsbBySerials) {
  // Tell apart different instances by their serial numbers.
  DeviceInfo device1{
      .instance_ids = std::vector<std::string>{"USB\\VID_1234&PID_5678"},
      .serial = "serial1",
      .version = "version1",
      .version_format = mojom::FwupdVersionFormat::kPlain,
      .joined_vendor_id = "USB:0x1234",
  };
  const auto& guid = InstanceIdToGuid("USB\\VID_1234&PID_5678");
  ASSERT_TRUE(guid.has_value());
  device1.guids.push_back(guid.value());

  DeviceInfo device2 = device1;
  device2.serial = "serial2";
  device2.version = "version2";

  std::vector<DeviceInfo> device_infos{device1, device2};

  auto usb_device_filter = UsbDeviceFilter{
      .vendor_id = 0x1234,
      .product_id = 0x5678,
      .serial = "serial1",
  };

  auto res = FetchUsbFirmwareVersion(device_infos, usb_device_filter);
  EXPECT_EQ(res->version, "version1");
  EXPECT_EQ(res->version_format, mojom::FwupdVersionFormat::kPlain);
}

TEST(FwupdUtilsTest, UsbProductNotMatched) {
  // Only the vendor id and the product name are matched but no product id and
  // no serial. This kind of matchingness is too weak.
  DeviceInfo device{
      .version = "version",
      .version_format = mojom::FwupdVersionFormat::kPlain,
      .joined_vendor_id = "USB:0x1234",
  };

  std::vector<DeviceInfo> device_infos{device};

  auto usb_device_filter = UsbDeviceFilter{
      .vendor_id = 0x1234,
      .product_id = 0x5678,
  };

  auto res = FetchUsbFirmwareVersion(device_infos, usb_device_filter);
  EXPECT_EQ(res.get(), nullptr);
}

TEST(FwupdUtilsTest, MultipleUsbMatchedWithACommonVersion) {
  // Multiple devices matches the VID:PID and the target serial number is
  // absent.
  DeviceInfo device{
      .instance_ids = std::vector<std::string>{"USB\\VID_1234&PID_5678"},
      .version = "version",
      .version_format = mojom::FwupdVersionFormat::kPlain,
      .joined_vendor_id = "USB:0x1234",
  };
  const auto& guid = InstanceIdToGuid("USB\\VID_1234&PID_5678");
  ASSERT_TRUE(guid.has_value());
  device.guids.push_back(guid.value());

  std::vector<DeviceInfo> device_infos{device, device};

  auto usb_device_filter = UsbDeviceFilter{
      .vendor_id = 0x1234,
      .product_id = 0x5678,
  };

  auto res = FetchUsbFirmwareVersion(device_infos, usb_device_filter);
  EXPECT_EQ(res->version, "version");
  EXPECT_EQ(res->version_format, mojom::FwupdVersionFormat::kPlain);
}

TEST(FwupdUtilsTest, MultipleUsbMatchedButDifferentVersions) {
  // Multiple matches but they have different versions.
  DeviceInfo device1{
      .instance_ids = std::vector<std::string>{"USB\\VID_1234&PID_5678"},
      .version = "version",
      .version_format = mojom::FwupdVersionFormat::kPlain,
      .joined_vendor_id = "USB:0x1234",
  };
  const auto& guid = InstanceIdToGuid("USB\\VID_1234&PID_5678");
  ASSERT_TRUE(guid.has_value());
  device1.guids.push_back(guid.value());

  DeviceInfo device2 = device1;
  device2.version = "version2";

  std::vector<DeviceInfo> device_infos{device1, device2};

  auto usb_device_filter = UsbDeviceFilter{
      .vendor_id = 0x1234,
      .product_id = 0x5678,
  };

  auto res = FetchUsbFirmwareVersion(device_infos, usb_device_filter);
  EXPECT_EQ(res.get(), nullptr);
}

}  // namespace
}  // namespace diagnostics::fwupd_utils
