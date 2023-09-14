// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/telemetry_for_testing.h"

#include <string>

namespace flex_hwis {

mojom::TelemetryInfoPtr TelemetryForTesting::MockSystemInfo() {
  auto system_info = mojom::SystemInfo::New();

  auto& dmi_info = system_info->dmi_info;
  dmi_info = mojom::DmiInfo::New();
  dmi_info->sys_vendor = kSystemVersion;
  dmi_info->product_name = kSystemProductName;
  dmi_info->product_version = kSystemProductVersion;
  dmi_info->bios_version = kSystemBiosVersion;

  auto& os_info = system_info->os_info;
  os_info = mojom::OsInfo::New();
  os_info->boot_mode = mojom::BootMode::kCrosSecure;

  info_->system_result =
      mojom::SystemResult::NewSystemInfo({std::move(system_info)});
  return std::move(info_);
}

mojom::TelemetryInfoPtr TelemetryForTesting::MockCpuInfo() {
  auto cpu_info = mojom::CpuInfo::New();

  auto& physical_cpus = cpu_info->physical_cpus;
  physical_cpus = std::vector<mojom::PhysicalCpuInfoPtr>(1);

  auto& physical_cpu = physical_cpus[0];
  physical_cpu = mojom::PhysicalCpuInfo::New();
  physical_cpu->model_name = kCpuModelName;

  info_->cpu_result = mojom::CpuResult::NewCpuInfo({std::move(cpu_info)});
  return std::move(info_);
}

mojom::TelemetryInfoPtr TelemetryForTesting::MockMemoryInfo() {
  auto memory_info = mojom::MemoryInfo::New();
  memory_info->total_memory_kib = kMemoryKib;

  info_->memory_result =
      mojom::MemoryResult::NewMemoryInfo({std::move(memory_info)});
  return std::move(info_);
}

mojom::TelemetryInfoPtr TelemetryForTesting::MockPciBusInfo(
    const mojom::BusDeviceClass controller, bool is_multiple) {
  auto bus_devices = std::vector<mojom::BusDevicePtr>(is_multiple ? 2 : 1);

  for (int i = 0; i < (is_multiple ? 2 : 1); i++) {
    auto& bus_device = bus_devices[i];
    bus_device = mojom::BusDevice::New();
    bus_device->vendor_name = kPciVendorName;
    bus_device->product_name = kBusProductName;
    bus_device->device_class = controller;

    auto pci_bus_info = mojom::PciBusInfo::New();
    pci_bus_info->vendor_id = i == 0 ? kPciBusVendorId : kSecondPciBusVendorId;
    pci_bus_info->device_id = i == 0 ? kPciBusDeviceId : kSecondPciBusDeviceId;
    pci_bus_info->driver = kPciBusDriver;

    auto& bus_info = bus_device->bus_info;
    bus_info = mojom::BusInfo::NewPciBusInfo({std::move(pci_bus_info)});
  }

  info_->bus_result = mojom::BusResult::NewBusDevices({std::move(bus_devices)});
  return std::move(info_);
}

mojom::TelemetryInfoPtr TelemetryForTesting::MockUsbBusInfo(
    const mojom::BusDeviceClass controller) {
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

  info_->bus_result = mojom::BusResult::NewBusDevices({std::move(bus_devices)});
  return std::move(info_);
}

mojom::TelemetryInfoPtr TelemetryForTesting::MockGraphicsInfo() {
  auto graphics_info = mojom::GraphicsInfo::New();
  auto& gles_info = graphics_info->gles_info;
  gles_info = mojom::GLESInfo::New();
  gles_info->version = kGraphicsVersion;
  gles_info->vendor = kGraphicsVendor;
  gles_info->renderer = kGraphicsRenderer;
  gles_info->shading_version = kGraphicsShadingVer;

  auto& extensions = gles_info->extensions;
  extensions = std::vector<std::string>(1);
  extensions[0] = kGraphicsExtension;

  info_->graphics_result =
      mojom::GraphicsResult::NewGraphicsInfo({std::move(graphics_info)});
  return std::move(info_);
}

mojom::TelemetryInfoPtr TelemetryForTesting::MockInputInfo() {
  auto input_info = mojom::InputInfo::New();
  input_info->touchpad_library_name = kTouchpadLibraryName;

  info_->input_result =
      mojom::InputResult::NewInputInfo({std::move(input_info)});
  return std::move(info_);
}

mojom::TelemetryInfoPtr TelemetryForTesting::MockTpmInfo() {
  auto tpm_info = mojom::TpmInfo::New();
  auto& version = tpm_info->version;
  version = mojom::TpmVersion::New();
  version->family = kTpmFamily;
  version->spec_level = kTpmSpecLevel;
  version->manufacturer = kTpmManufacturer;

  tpm_info->did_vid = kTpmDidVid;

  auto& supported_features = tpm_info->supported_features;
  supported_features = mojom::TpmSupportedFeatures::New();
  supported_features->is_allowed = kTpmIsAllowed;

  auto& status = tpm_info->status;
  status = mojom::TpmStatus::New();
  status->owned = kTpmOwned;

  info_->tpm_result = mojom::TpmResult::NewTpmInfo({std::move(tpm_info)});
  return std::move(info_);
}

mojom::TelemetryInfoPtr TelemetryForTesting::MockTelemetryInfo() {
  info_ = MockSystemInfo();
  info_ = MockCpuInfo();
  info_ = MockMemoryInfo();
  info_ = MockPciBusInfo(mojom::BusDeviceClass::kEthernetController, false);
  info_ = MockGraphicsInfo();
  info_ = MockInputInfo();
  info_ = MockTpmInfo();
  return std::move(info_);
}
}  // namespace flex_hwis
