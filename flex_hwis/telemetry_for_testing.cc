// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/telemetry_for_testing.h"

#include <string>

namespace flex_hwis {

void TelemetryForTesting::AddSystemInfo() {
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
}

void TelemetryForTesting::AddCpuInfo() {
  auto cpu_info = mojom::CpuInfo::New();

  auto& physical_cpus = cpu_info->physical_cpus;
  physical_cpus = std::vector<mojom::PhysicalCpuInfoPtr>(1);

  auto& physical_cpu = physical_cpus[0];
  physical_cpu = mojom::PhysicalCpuInfo::New();
  physical_cpu->model_name = kCpuModelName;

  info_->cpu_result = mojom::CpuResult::NewCpuInfo({std::move(cpu_info)});
}

void TelemetryForTesting::AddMemoryInfo() {
  auto memory_info = mojom::MemoryInfo::New();
  memory_info->total_memory_kib = kMemoryKib;

  info_->memory_result =
      mojom::MemoryResult::NewMemoryInfo({std::move(memory_info)});
}

void TelemetryForTesting::AddPciBusInfo(const mojom::BusDeviceClass dev_class) {
  AddPciBusInfo(dev_class, kPciVendorName, kBusProductName, kPciBusVendorId,
                kPciBusDeviceId, kPciBusDriver);
}

void TelemetryForTesting::AddPciBusInfo(const mojom::BusDeviceClass dev_class,
                                        const std::string& vendor,
                                        const std::string& product,
                                        uint16_t vendor_id,
                                        uint16_t device_id,
                                        const std::string& driver) {
  // We don't use these three, just leave them blank;
  uint8_t class_id = 0;
  uint8_t subclass_id = 0;
  uint8_t prog_if_id = 0;
  auto bus_info = mojom::PciBusInfo::New(class_id, subclass_id, prog_if_id,
                                         vendor_id, device_id, driver);

  devices_.push_back(mojom::BusDevice::New(
      vendor, product, dev_class,
      mojom::BusInfo::NewPciBusInfo(std::move(bus_info))));
}

void TelemetryForTesting::AddUsbBusInfo(const mojom::BusDeviceClass dev_class) {
  AddUsbBusInfo(dev_class, kUsbVendorName, kBusProductName, kPciBusVendorId,
                kPciBusDeviceId, kPciBusDriver);
}

void TelemetryForTesting::AddUsbBusInfo(const mojom::BusDeviceClass dev_class,
                                        const std::string& vendor,
                                        const std::string& product,
                                        uint16_t vendor_id,
                                        uint16_t product_id,
                                        const std::string& driver) {
  // We don't use these four, just leave them blank;
  uint8_t interface_number = 0;
  uint8_t class_id = 0;
  uint8_t subclass_id = 0;
  uint8_t protocol_id = 0;
  std::vector<mojom::UsbBusInterfaceInfoPtr> interfaces;
  interfaces.push_back(mojom::UsbBusInterfaceInfo::New(
      interface_number, class_id, subclass_id, protocol_id, driver));
  auto bus_info =
      mojom::UsbBusInfo::New(class_id, subclass_id, protocol_id, vendor_id,
                             product_id, std::move(interfaces));

  devices_.push_back(mojom::BusDevice::New(
      vendor, product, dev_class,
      mojom::BusInfo::NewUsbBusInfo(std::move(bus_info))));
}

void TelemetryForTesting::AddGraphicsInfo() {
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
}

void TelemetryForTesting::AddInputInfo() {
  auto input_info = mojom::InputInfo::New();
  input_info->touchpad_library_name = kTouchpadLibraryName;

  info_->input_result =
      mojom::InputResult::NewInputInfo({std::move(input_info)});
}

void TelemetryForTesting::AddTpmInfo() {
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
}

void TelemetryForTesting::AddTelemetryInfo() {
  AddSystemInfo();
  AddCpuInfo();
  AddMemoryInfo();
  AddPciBusInfo(mojom::BusDeviceClass::kEthernetController);
  AddGraphicsInfo();
  AddInputInfo();
  AddTpmInfo();
}

mojom::TelemetryInfoPtr TelemetryForTesting::Get() const {
  std::vector<mojom::BusDevicePtr> devices_copy;
  for (auto& dev : devices_) {
    devices_copy.push_back(dev.Clone());
  }
  info_->bus_result = mojom::BusResult::NewBusDevices(std::move(devices_copy));
  return info_.Clone();
}

}  // namespace flex_hwis
