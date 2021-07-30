// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/check_op.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/common/mojo_type_utils.h"
#include "diagnostics/cros_healthd/fetchers/bus_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bus_fetcher_constants.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

using chromeos::cros_healthd::mojom::ThunderboltSecurityLevel;

constexpr char kFakePathPciDevices[] = "sys/devices/pci0000:00";
constexpr char kLinkPciDevices[] = "../../../devices/pci0000:00";
constexpr char kFakePathUsbDevices[] =
    "sys/devices/pci0000:00/0000:00:14.0/usb1";
constexpr char kFakeThunderboltDevices[] = "sys/bus/thunderbolt/devices";
constexpr char kLinkUsbDevices[] =
    "../../../devices/pci0000:00/0000:00:14.0/usb1";
constexpr char kLinkPciDriver[] = "../../../bus/pci/drivers";
constexpr char kLinkUsbDriver[] = "../../../../../../bus/usb/drivers";

constexpr char kFakePciVendorName[] = "Vendor:12AB";
constexpr char kFakePciProductName[] = "Device:34CD";
constexpr char kFakeUsbVendorName[] = "usb:v12ABp34CD";
constexpr auto kFakeUsbProductName = kFakeUsbVendorName;
constexpr auto kFakeUsbFallbackVendorName = "Fallback Vendor Name";
constexpr auto kFakeUsbFallbackProductName = "Fallback Product Name";
constexpr uint8_t kFakeClass = 0x0a;
constexpr uint8_t kFakeSubclass = 0x1b;
constexpr uint8_t kFakeProg = 0x2c;
constexpr uint8_t kFakeProtocol = kFakeProg;
constexpr uint16_t kFakeVendor = 0x12ab;
constexpr uint16_t kFakeDevice = 0x34cd;
constexpr char kFakeDriver[] = "driver";

constexpr char kFakeThunderboltDeviceVendorName[] =
    "FakeThunderboltDeviceVendor";
constexpr char kFakeThunderboltDeviceName[] = "FakeThunderboltDevice";
constexpr uint8_t kFakeThunderboltDeviceAuthorized = false;
constexpr char kFakeThunderboltDeviceSpeedStr[] = "20.0 Gb/s";
constexpr uint32_t kFakeThunderboltDeviceSpeed = 20;
constexpr char kFakeThunderboltDeviceType[] = "0x4257";
constexpr char kFakeThunderboltDeviceUUID[] =
    "d5010000-0060-6508-2304-61066ed3f91e";
constexpr char kFakeThunderboltDeviceFWVer[] = "29.0";

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

std::string ToFixHexStr(uint8_t val) {
  return base::StringPrintf("%02x", val);
}

std::string ToFixHexStr(uint16_t val) {
  return base::StringPrintf("%04x", val);
}

class BusFetcherTest : public BaseFileTest {
 public:
  BusFetcherTest() = default;
  BusFetcherTest(const BusFetcherTest&) = delete;
  BusFetcherTest& operator=(const BusFetcherTest&) = delete;

  void SetUp() override {
    SetTestRoot(mock_context_.root_dir());
  }

  mojo_ipc::BusDevicePtr& AddExpectedPciDevice() {
    auto device = mojo_ipc::BusDevice::New();
    auto pci_info = mojo_ipc::PciBusInfo::New();

    device->vendor_name = kFakePciVendorName;
    device->product_name = kFakePciProductName;
    device->device_class = mojo_ipc::BusDeviceClass::kOthers;
    pci_info->class_id = kFakeClass;
    pci_info->subclass_id = kFakeSubclass;
    pci_info->prog_if_id = kFakeProg;
    pci_info->vendor_id = kFakeVendor;
    pci_info->device_id = kFakeDevice;
    pci_info->driver = kFakeDriver;

    device->bus_info = mojo_ipc::BusInfo::NewPciBusInfo(std::move(pci_info));
    expected_bus_devices_.push_back(std::move(device));
    return expected_bus_devices_.back();
  }

  mojo_ipc::BusDevicePtr& AddExpectedUsbDevice(size_t interface_count) {
    CHECK_GE(interface_count, 1);
    auto device = mojo_ipc::BusDevice::New();
    auto usb_info = mojo_ipc::UsbBusInfo::New();

    device->vendor_name = kFakeUsbVendorName;
    device->product_name = kFakeUsbProductName;
    device->device_class = mojo_ipc::BusDeviceClass::kOthers;
    usb_info->class_id = kFakeClass;
    usb_info->subclass_id = kFakeSubclass;
    usb_info->protocol_id = kFakeProtocol;
    usb_info->vendor_id = kFakeVendor;
    usb_info->product_id = kFakeDevice;
    for (size_t i = 0; i < interface_count; ++i) {
      auto usb_if_info = mojo_ipc::UsbBusInterfaceInfo::New();
      usb_if_info->interface_number = static_cast<uint8_t>(i);
      usb_if_info->class_id = kFakeClass;
      usb_if_info->subclass_id = kFakeSubclass;
      usb_if_info->protocol_id = kFakeProtocol;
      usb_if_info->driver = kFakeDriver;
      usb_info->interfaces.push_back(std::move(usb_if_info));
    }

    device->bus_info = mojo_ipc::BusInfo::NewUsbBusInfo(std::move(usb_info));
    expected_bus_devices_.push_back(std::move(device));
    return expected_bus_devices_.back();
  }

  mojo_ipc::BusDevicePtr& AddExpectedThunderboltDevice(size_t interface_count) {
    CHECK_GE(interface_count, 1);
    auto device = mojo_ipc::BusDevice::New();
    auto tbt_info = mojo_ipc::ThunderboltBusInfo::New();

    device->device_class = mojo_ipc::BusDeviceClass::kThunderboltController;
    tbt_info->security_level = mojo_ipc::ThunderboltSecurityLevel::kNone;
    for (size_t i = 0; i < interface_count; ++i) {
      auto tbt_if_info = mojo_ipc::ThunderboltBusInterfaceInfo::New();
      tbt_if_info->authorized = kFakeThunderboltDeviceAuthorized;
      tbt_if_info->rx_speed_gbs = kFakeThunderboltDeviceSpeed;
      tbt_if_info->tx_speed_gbs = kFakeThunderboltDeviceSpeed;
      tbt_if_info->vendor_name = kFakeThunderboltDeviceVendorName;
      tbt_if_info->device_name = kFakeThunderboltDeviceName;
      tbt_if_info->device_type = kFakeThunderboltDeviceType;
      tbt_if_info->device_uuid = kFakeThunderboltDeviceUUID;
      tbt_if_info->device_fw_version = kFakeThunderboltDeviceFWVer;
      tbt_info->thunderbolt_interfaces.push_back(std::move(tbt_if_info));
    }
    device->bus_info =
        mojo_ipc::BusInfo::NewThunderboltBusInfo(std::move(tbt_info));
    expected_bus_devices_.push_back(std::move(device));
    return expected_bus_devices_.back();
  }

  void SetExpectedBusDevices() {
    for (size_t i = 0; i < expected_bus_devices_.size(); ++i) {
      const auto& bus_info = expected_bus_devices_[i]->bus_info;
      switch (bus_info->which()) {
        case mojo_ipc::BusInfo::Tag::PCI_BUS_INFO:
          SetPciBusInfo(bus_info->get_pci_bus_info(), i);
          break;
        case mojo_ipc::BusInfo::Tag::USB_BUS_INFO:
          SetUsbBusInfo(bus_info->get_usb_bus_info(), i);
          break;
        case mojo_ipc::BusInfo::Tag::THUNDERBOLT_BUS_INFO:
          SetThunderboltBusInfo(bus_info->get_thunderbolt_bus_info(), i);
          break;
      }
    }
  }

  void SetPciBusInfo(const mojo_ipc::PciBusInfoPtr& pci_info, size_t id) {
    const auto dir = kFakePathPciDevices;
    const auto dev = base::StringPrintf("0000:00:%02zx.0", id);
    SetSymbolicLink({kLinkPciDevices, dev}, {kPathSysPci, dev});

    auto class_str =
        base::StringPrintf("%#02x%02x%02x", pci_info->class_id,
                           pci_info->subclass_id, pci_info->prog_if_id);
    SetFile({dir, dev, kFilePciClass}, class_str);
    SetFile({dir, dev, kFilePciVendor},
            "0x" + ToFixHexStr(pci_info->vendor_id));
    SetFile({dir, dev, kFilePciDevice},
            "0x" + ToFixHexStr(pci_info->device_id));
    if (pci_info->driver) {
      SetSymbolicLink({kLinkPciDriver, pci_info->driver.value()},
                      {dir, dev, kFileDriver});
    }
  }

  void SetUsbBusInfo(const mojo_ipc::UsbBusInfoPtr& usb_info, size_t id) {
    const auto dir = kFakePathUsbDevices;
    const auto dev = base::StringPrintf("1-%zu", id);
    SetSymbolicLink({kLinkUsbDevices, dev}, {kPathSysUsb, dev});

    SetFile({dir, dev, kFileUsbDevClass}, ToFixHexStr(usb_info->class_id));
    SetFile({dir, dev, kFileUsbDevSubclass},
            ToFixHexStr(usb_info->subclass_id));
    SetFile({dir, dev, kFileUsbDevProtocol},
            ToFixHexStr(usb_info->protocol_id));
    SetFile({dir, dev, kFileUsbVendor}, ToFixHexStr(usb_info->vendor_id));
    SetFile({dir, dev, kFileUsbProduct}, ToFixHexStr(usb_info->product_id));
    SetFile({dir, dev, kFileUsbManufacturerName}, kFakeUsbFallbackVendorName);
    SetFile({dir, dev, kFileUsbProductName}, kFakeUsbFallbackProductName);

    for (size_t i = 0; i < usb_info->interfaces.size(); ++i) {
      const auto dev_if = base::StringPrintf("1-%zu:1.%zu", id, i);
      const mojo_ipc::UsbBusInterfaceInfoPtr& usb_if_info =
          usb_info->interfaces[i];

      ASSERT_EQ(usb_if_info->interface_number, static_cast<uint8_t>(i));
      SetFile({dir, dev, dev_if, kFileUsbIFNumber},
              ToFixHexStr(usb_if_info->interface_number));
      SetFile({dir, dev, dev_if, kFileUsbIFClass},
              ToFixHexStr(usb_if_info->class_id));
      SetFile({dir, dev, dev_if, kFileUsbIFSubclass},
              ToFixHexStr(usb_if_info->subclass_id));
      SetFile({dir, dev, dev_if, kFileUsbIFProtocol},
              ToFixHexStr(usb_if_info->protocol_id));
      if (usb_if_info->driver) {
        SetSymbolicLink({kLinkUsbDriver, usb_if_info->driver.value()},
                        {dir, dev, dev_if, kFileDriver});
      }
    }
  }

  std::string EnumToString(ThunderboltSecurityLevel level) {
    switch (level) {
      case ThunderboltSecurityLevel::kNone:
        return "None";
      case ThunderboltSecurityLevel::kUserLevel:
        return "User";
      case ThunderboltSecurityLevel::kSecureLevel:
        return "Secure";
      case ThunderboltSecurityLevel::kDpOnlyLevel:
        return "DpOnly";
      case ThunderboltSecurityLevel::kUsbOnlyLevel:
        return "UsbOnly";
      case ThunderboltSecurityLevel::kNoPcieLevel:
        return "NoPcie";
    }
  }

  void SetThunderboltBusInfo(const mojo_ipc::ThunderboltBusInfoPtr& tbt_info,
                             size_t id) {
    const auto dir = kFakeThunderboltDevices;
    const auto dev = base::StringPrintf("domain%zu/", id);
    SetFile({dir, dev, kFileThunderboltSecurity},
            EnumToString(tbt_info->security_level));

    for (size_t i = 0; i < tbt_info->thunderbolt_interfaces.size(); ++i) {
      const auto dev_if = base::StringPrintf("%zu-%zu:%zu.%zu", id, id, id, i);
      const mojo_ipc::ThunderboltBusInterfaceInfoPtr& tbt_if_info =
          tbt_info->thunderbolt_interfaces[i];
      SetFile({dir, dev_if, kFileThunderboltAuthorized},
              tbt_if_info->authorized ? "1" : "0");
      SetFile({dir, dev_if, kFileThunderboltRxSpeed},
              kFakeThunderboltDeviceSpeedStr);
      SetFile({dir, dev_if, kFileThunderboltTxSpeed},
              kFakeThunderboltDeviceSpeedStr);
      SetFile({dir, dev_if, kFileThunderboltVendorName},
              tbt_if_info->vendor_name);
      SetFile({dir, dev_if, kFileThunderboltDeviceName},
              tbt_if_info->device_name);
      SetFile({dir, dev_if, kFileThunderboltDeviceType},
              tbt_if_info->device_type);
      SetFile({dir, dev_if, kFileThunderboltUUID}, tbt_if_info->device_uuid);
      SetFile({dir, dev_if, kFileThunderboltFWVer},
              tbt_if_info->device_fw_version);
    }
  }

  void FetchBusDevices() {
    auto res = bus_fetcher_.FetchBusDevices();
    ASSERT_TRUE(res->is_bus_devices());
    const auto& bus_devices = res->get_bus_devices();
    const auto got = Sorted(bus_devices);
    const auto expected = Sorted(expected_bus_devices_);
    EXPECT_EQ(got, expected) << GetDiffString(got, expected);
  }

 protected:
  std::vector<mojo_ipc::BusDevicePtr> expected_bus_devices_;
  MockContext mock_context_;
  BusFetcher bus_fetcher_{&mock_context_};
};

TEST_F(BusFetcherTest, TestFetchPci) {
  AddExpectedPciDevice();
  SetExpectedBusDevices();
  FetchBusDevices();
}

TEST_F(BusFetcherTest, TestFetchUsbBusInfo) {
  AddExpectedUsbDevice(1);
  SetExpectedBusDevices();
  FetchBusDevices();
}

TEST_F(BusFetcherTest, TestFetchThunderboltBusInfo) {
  AddExpectedThunderboltDevice(1);
  SetExpectedBusDevices();
  FetchBusDevices();
}

TEST_F(BusFetcherTest, TestFetchMultiple) {
  AddExpectedPciDevice();
  AddExpectedPciDevice();
  AddExpectedPciDevice();
  AddExpectedUsbDevice(1);
  AddExpectedUsbDevice(2);
  AddExpectedUsbDevice(3);
  AddExpectedThunderboltDevice(1);
  AddExpectedThunderboltDevice(2);
  SetExpectedBusDevices();
  FetchBusDevices();
}

TEST_F(BusFetcherTest, TestUsbFallback) {
  auto& bus_info = AddExpectedUsbDevice(1);
  bus_info->vendor_name = kFakeUsbFallbackVendorName;
  bus_info->product_name = kFakeUsbFallbackProductName;
  mock_context_.fake_udev()->fake_udev_hwdb()->SetReturnEmptyProperties(true);
  SetExpectedBusDevices();
  FetchBusDevices();
}

}  // namespace
}  // namespace diagnostics
