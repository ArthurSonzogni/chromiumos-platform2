// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_MOCK_MOJO_H_
#define FLEX_HWIS_MOCK_MOJO_H_

#include <utility>
#include <vector>

#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

constexpr char kSystemVersion[] = "LENOVO";
constexpr char kSystemProductName[] = "20HQS1MX00";
constexpr char kSystemProductVersion[] = "ThinkPad X1 Carbon 5th";
constexpr char kSystemBiosVersion[] = "N1MET37W";
const bool kSystemUefi = false;
constexpr char kCpuModelName[] = "Mock CPU Model";
constexpr int kMemoryKib = 16131552;
constexpr char kPciId[] = "pci:0bda:8153";
constexpr char kSecondPciId[] = "pci:8086:2a43";
constexpr char kUsbId[] = "usb:0bda:8153";
constexpr char kBusPciName[] = "Mock PCI Vendor I219-LM";
constexpr char kBusUsbName[] = "Mock USB Vendor I219-LM";
constexpr char kDriver[] = "r8152";
constexpr char kGraphicsVersion[] = "OpenGL ES 3.2 Mesa 22.3.3";
constexpr char kGraphicsVendor[] = "Intel";
constexpr char kGraphicsRenderer[] = "Mesa Intel(R) HD Graphics 620";
constexpr char kGraphicsShadingVer[] = "OpenGL ES GLSL ES 3.20";
constexpr char kGraphicsExtension[] = "GL_EXT_blend_minmax";
constexpr char kTouchpadLibraryName[] = "gestures";
constexpr int kTpmFamily = 0x322e3000;
constexpr char kTpmFamilyStr[] = "2.0";
constexpr int kTpmSpecLevel = 0;
constexpr int kTpmManufacturer = 0;
constexpr char kTpmDidVid[] = "test";
constexpr char kPciVendorName[] = "Mock PCI Vendor";
constexpr char kUsbVendorName[] = "Mock USB Vendor";
constexpr char kBusProductName[] = "I219-LM";
constexpr int kPciBusVendorId = 0x0bda;
constexpr int kPciBusDeviceId = 0x8153;
constexpr int kSecondPciBusVendorId = 0x8086;
constexpr int kSecondPciBusDeviceId = 0x2a43;
constexpr char kPciBusDriver[] = "r8152";
constexpr bool kTpmIsAllowed = true;
constexpr bool kTpmOwned = true;
constexpr char kUuid[] = "reven-uuid";

namespace flex_hwis {
namespace mojom = ::ash::cros_healthd::mojom;

class MockMojo {
 public:
  // Create a mock system information for testing purposes.
  mojom::TelemetryInfoPtr MockSystemInfo();
  // Create a mock cpu information for testing purposes.
  mojom::TelemetryInfoPtr MockCpuInfo();
  // Create a mock memory information for testing purposes.
  mojom::TelemetryInfoPtr MockMemoryInfo();
  // Create a mock pci bus information for testing purposes.
  template <class T>
  mojom::TelemetryInfoPtr MockPciBusInfo(const T& controller,
                                         bool is_multiple) {
    auto bus_devices = std::vector<mojom::BusDevicePtr>(is_multiple ? 2 : 1);

    for (int i = 0; i < (is_multiple ? 2 : 1); i++) {
      auto& bus_device = bus_devices[i];
      bus_device = mojom::BusDevice::New();
      bus_device->vendor_name = kPciVendorName;
      bus_device->product_name = kBusProductName;
      bus_device->device_class = controller;

      auto pci_bus_info = mojom::PciBusInfo::New();
      pci_bus_info->vendor_id =
          i == 0 ? kPciBusVendorId : kSecondPciBusVendorId;
      pci_bus_info->device_id =
          i == 0 ? kPciBusDeviceId : kSecondPciBusDeviceId;
      pci_bus_info->driver = kPciBusDriver;

      auto& bus_info = bus_device->bus_info;
      bus_info = mojom::BusInfo::NewPciBusInfo({std::move(pci_bus_info)});
    }

    info_->bus_result =
        mojom::BusResult::NewBusDevices({std::move(bus_devices)});
    return std::move(info_);
  }

  // Create a mock usb bus information for testing purposes.
  template <class T>
  mojom::TelemetryInfoPtr MockUsbBusInfo(const T& controller) {
    auto bus_devices = std::vector<mojom::BusDevicePtr>(1);
    auto& bus_device = bus_devices[0];

    bus_device = mojom::BusDevice::New();
    bus_device->vendor_name = kUsbVendorName;
    bus_device->product_name = kBusProductName;
    bus_device->device_class = controller;

    auto usb_bus_info = mojom::UsbBusInfo::New();
    usb_bus_info->vendor_id = kPciBusVendorId;
    usb_bus_info->product_id = kPciBusDeviceId;

    auto& interfaces = usb_bus_info->interfaces;
    interfaces = std::vector<mojom::UsbBusInterfaceInfoPtr>(1);

    auto& interface = interfaces[0];
    interface = mojom::UsbBusInterfaceInfo::New();
    interface->driver = kPciBusDriver;

    auto& bus_info = bus_device->bus_info;
    bus_info = mojom::BusInfo::NewUsbBusInfo({std::move(usb_bus_info)});

    info_->bus_result =
        mojom::BusResult::NewBusDevices({std::move(bus_devices)});
    return std::move(info_);
  }
  // Create a mock graphics information for testing purposes.
  mojom::TelemetryInfoPtr MockGraphicsInfo();
  // Create a mock input information for testing purposes.
  mojom::TelemetryInfoPtr MockInputInfo();
  // Create a mock tpm information for testing purposes.
  mojom::TelemetryInfoPtr MockTpmInfo();
  // Create a total mock telemetry information for testing purposes.
  mojom::TelemetryInfoPtr MockTelemetryInfo();

 private:
  mojom::TelemetryInfoPtr info_ = mojom::TelemetryInfo::New();
};

}  // namespace flex_hwis

#endif  // FLEX_HWIS_MOCK_MOJO_H_
