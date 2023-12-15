// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_TELEMETRY_FOR_TESTING_H_
#define FLEX_HWIS_TELEMETRY_FOR_TESTING_H_

#include <string>
#include <utility>
#include <vector>

#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

inline constexpr char kSystemVersion[] = "LENOVO";
inline constexpr char kSystemProductName[] = "20HQS1MX00";
inline constexpr char kSystemProductVersion[] = "ThinkPad X1 Carbon 5th";
inline constexpr char kSystemBiosVersion[] = "N1MET37W";
inline constexpr bool kSystemUefi = false;
inline constexpr char kCpuModelName[] = "Mock CPU Model";
inline constexpr int kMemoryKib = 16131552;
inline constexpr char kPciId[] = "pci:0bda:8153";
inline constexpr char kSecondPciId[] = "pci:8086:2a43";
inline constexpr char kUsbId[] = "usb:0bda:8153";
inline constexpr char kBusPciName[] = "Mock PCI Vendor I219-LM";
inline constexpr char kBusUsbName[] = "Mock USB Vendor I219-LM";
inline constexpr char kDriver[] = "r8152";
inline constexpr char kGraphicsVersion[] = "OpenGL ES 3.2 Mesa 22.3.3";
inline constexpr char kGraphicsVendor[] = "Intel";
inline constexpr char kGraphicsRenderer[] = "Mesa Intel(R) HD Graphics 620";
inline constexpr char kGraphicsShadingVer[] = "OpenGL ES GLSL ES 3.20";
inline constexpr char kGraphicsExtension[] = "GL_EXT_blend_minmax";
inline constexpr char kTouchpadLibraryName[] = "gestures";
inline constexpr int kTpmFamily = 0x322e3000;
inline constexpr char kTpmFamilyStr[] = "2.0";
inline constexpr int kTpmSpecLevel = 0;
inline constexpr int kTpmManufacturer = 0;
inline constexpr char kTpmDidVid[] = "test";
inline constexpr char kPciVendorName[] = "Mock PCI Vendor";
inline constexpr char kUsbVendorName[] = "Mock USB Vendor";
inline constexpr char kBusProductName[] = "I219-LM";
inline constexpr int kPciBusVendorId = 0x0bda;
inline constexpr int kPciBusDeviceId = 0x8153;
inline constexpr int kSecondPciBusVendorId = 0x8086;
inline constexpr int kSecondPciBusDeviceId = 0x2a43;
inline constexpr char kPciBusDriver[] = "r8152";
inline constexpr bool kTpmIsAllowed = true;
inline constexpr bool kTpmOwned = true;

namespace flex_hwis {
namespace mojom = ::ash::cros_healthd::mojom;

// Builds a mojom::TelemetryInfoPtr for use in tests.
class TelemetryForTesting {
 public:
  // Fill in default fake system information.
  void AddSystemInfo();
  // Fill in default fake cpu information.
  void AddCpuInfo();
  // Fill in default fake memory information.
  void AddMemoryInfo();
  // Fill in default fake pci bus information with a specified device class.
  void AddPciBusInfo(const mojom::BusDeviceClass dev_class);
  // Fill in fake pci bus information.
  void AddPciBusInfo(const mojom::BusDeviceClass dev_class,
                     const std::string& vendor,
                     const std::string& product,
                     uint16_t vendor_id,
                     uint16_t device_id,
                     const std::string& driver);
  // Fill in default fake usb bus information with a specified device class.
  void AddUsbBusInfo(const mojom::BusDeviceClass dev_class);
  // Fill in fake usb bus information.
  void AddUsbBusInfo(const mojom::BusDeviceClass dev_class,
                     const std::string& vendor,
                     const std::string& product,
                     uint16_t vendor_id,
                     uint16_t product_id,
                     const std::string& driver);
  // Fill in default fake graphics information.
  void AddGraphicsInfo();
  // Fill in default fake input information.
  void AddInputInfo();
  // Fill in default fake tpm information.
  void AddTpmInfo();
  // Fill in all the information.
  void AddTelemetryInfo();

  // Returns a copy of the TelemetryInfoPtr we've been building.
  mojom::TelemetryInfoPtr Get() const;

 private:
  mojom::TelemetryInfoPtr info_ = mojom::TelemetryInfo::New();
  // Manipulating the bus_result directly is difficult.
  // Collect devices to add at the last minute.
  std::vector<mojom::BusDevicePtr> devices_;
};

}  // namespace flex_hwis

#endif  // FLEX_HWIS_TELEMETRY_FOR_TESTING_H_
