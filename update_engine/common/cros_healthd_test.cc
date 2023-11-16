//
// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/common/cros_healthd.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace chromeos_update_engine {

TEST(CrosHealthdTest, ParseSystemResultCheck) {
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    CrosHealthd::ParseSystemResult(&telemetry_info_ptr, &telemetry_info);
    EXPECT_EQ("", telemetry_info.system_info.dmi_info.sys_vendor);
    EXPECT_EQ("", telemetry_info.system_info.dmi_info.product_name);
    EXPECT_EQ("", telemetry_info.system_info.dmi_info.product_version);
    EXPECT_EQ("", telemetry_info.system_info.dmi_info.bios_version);
    EXPECT_EQ(TelemetryInfo::SystemInfo::OsInfo::BootMode::kUnknown,
              telemetry_info.system_info.os_info.boot_mode);
  }
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    telemetry_info_ptr->system_result =
        ash::cros_healthd::mojom::SystemResult::NewSystemInfo(
            ash::cros_healthd::mojom::SystemInfo::New());
    auto& system_info_ptr =
        telemetry_info_ptr->system_result->get_system_info();

    system_info_ptr->dmi_info = ash::cros_healthd::mojom::DmiInfo::New();
    auto& dmi_info_ptr = system_info_ptr->dmi_info;
    // Missing values.
    dmi_info_ptr->product_name = "fake-product-name";
    dmi_info_ptr->bios_version = "fake-bios-version";

    system_info_ptr->os_info = ash::cros_healthd::mojom::OsInfo::New();
    auto& os_info_ptr = system_info_ptr->os_info;
    os_info_ptr->boot_mode = ash::cros_healthd::mojom::BootMode::kCrosEfi;

    CrosHealthd::ParseSystemResult(&telemetry_info_ptr, &telemetry_info);
    EXPECT_EQ("fake-product-name",
              telemetry_info.system_info.dmi_info.product_name);
    EXPECT_EQ("fake-bios-version",
              telemetry_info.system_info.dmi_info.bios_version);
    EXPECT_EQ(TelemetryInfo::SystemInfo::OsInfo::BootMode::kCrosEfi,
              telemetry_info.system_info.os_info.boot_mode);
  }
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    telemetry_info_ptr->system_result =
        ash::cros_healthd::mojom::SystemResult::NewSystemInfo(
            ash::cros_healthd::mojom::SystemInfo::New());
    auto& system_info_ptr =
        telemetry_info_ptr->system_result->get_system_info();

    system_info_ptr->dmi_info = ash::cros_healthd::mojom::DmiInfo::New();
    auto& dmi_info_ptr = system_info_ptr->dmi_info;
    dmi_info_ptr->sys_vendor = "fake-sys-vendor";
    dmi_info_ptr->product_name = "fake-product-name";
    dmi_info_ptr->product_version = "fake-product-version";
    dmi_info_ptr->bios_version = "fake-bios-version";

    system_info_ptr->os_info = ash::cros_healthd::mojom::OsInfo::New();
    auto& os_info_ptr = system_info_ptr->os_info;
    os_info_ptr->boot_mode = ash::cros_healthd::mojom::BootMode::kCrosEfi;

    CrosHealthd::ParseSystemResult(&telemetry_info_ptr, &telemetry_info);
    EXPECT_EQ("fake-sys-vendor",
              telemetry_info.system_info.dmi_info.sys_vendor);
    EXPECT_EQ("fake-product-name",
              telemetry_info.system_info.dmi_info.product_name);
    EXPECT_EQ("fake-product-version",
              telemetry_info.system_info.dmi_info.product_version);
    EXPECT_EQ("fake-bios-version",
              telemetry_info.system_info.dmi_info.bios_version);
    EXPECT_EQ(TelemetryInfo::SystemInfo::OsInfo::BootMode::kCrosEfi,
              telemetry_info.system_info.os_info.boot_mode);
  }
}

TEST(CrosHealthdTest, ParseMemoryResultCheck) {
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    CrosHealthd::ParseMemoryResult(&telemetry_info_ptr, &telemetry_info);
    EXPECT_EQ(uint32_t(0), telemetry_info.memory_info.total_memory_kib);
  }
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    telemetry_info_ptr->memory_result =
        ash::cros_healthd::mojom::MemoryResult::NewMemoryInfo(
            ash::cros_healthd::mojom::MemoryInfo::New());

    auto& memory_info_ptr =
        telemetry_info_ptr->memory_result->get_memory_info();
    memory_info_ptr->total_memory_kib = uint32_t(123);

    CrosHealthd::ParseMemoryResult(&telemetry_info_ptr, &telemetry_info);
    EXPECT_EQ(uint32_t(123), telemetry_info.memory_info.total_memory_kib);
  }
}

TEST(CrosHealthdTest, ParseNonRemovableBlockDeviceResultCheck) {
  {
    TelemetryInfo telemetry_info{};
    auto telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    CrosHealthd::ParseNonRemovableBlockDeviceResult(&telemetry_info_ptr,
                                                    &telemetry_info);
    EXPECT_TRUE(telemetry_info.block_device_info.empty());
  }
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    std::vector<ash::cros_healthd::mojom::NonRemovableBlockDeviceInfoPtr>
        block_device_info;
    block_device_info.emplace_back(
        ash::cros_healthd::mojom::NonRemovableBlockDeviceInfo::New());
    telemetry_info_ptr->block_device_result =
        ash::cros_healthd::mojom::NonRemovableBlockDeviceResult::
            NewBlockDeviceInfo(std::move(block_device_info));

    auto& block_device_info_ptr =
        telemetry_info_ptr->block_device_result->get_block_device_info();
    block_device_info_ptr.front()->size = uint64_t(123);

    CrosHealthd::ParseNonRemovableBlockDeviceResult(&telemetry_info_ptr,
                                                    &telemetry_info);
    EXPECT_EQ(uint64_t(123), telemetry_info.block_device_info.front().size);
  }
}

TEST(CrosHealthdTest, ParseCpuResultCheck) {
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    CrosHealthd::ParseCpuResult(&telemetry_info_ptr, &telemetry_info);
    EXPECT_TRUE(telemetry_info.cpu_info.physical_cpus.empty());
  }
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    telemetry_info_ptr->cpu_result =
        ash::cros_healthd::mojom::CpuResult::NewCpuInfo(
            ash::cros_healthd::mojom::CpuInfo::New());
    auto& cpu_info_ptr = telemetry_info_ptr->cpu_result->get_cpu_info();

    std::vector<ash::cros_healthd::mojom::PhysicalCpuInfoPtr> physical_cpus;
    physical_cpus.emplace_back(
        ash::cros_healthd::mojom::PhysicalCpuInfo::New());
    cpu_info_ptr->physical_cpus = std::move(physical_cpus);

    // Missing values, don't set any values.

    CrosHealthd::ParseCpuResult(&telemetry_info_ptr, &telemetry_info);
    EXPECT_TRUE(telemetry_info.cpu_info.physical_cpus.empty());
  }
  {
    TelemetryInfo telemetry_info{};
    auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
    telemetry_info_ptr->cpu_result =
        ash::cros_healthd::mojom::CpuResult::NewCpuInfo(
            ash::cros_healthd::mojom::CpuInfo::New());
    auto& cpu_info_ptr = telemetry_info_ptr->cpu_result->get_cpu_info();

    std::vector<ash::cros_healthd::mojom::PhysicalCpuInfoPtr> physical_cpus;
    physical_cpus.emplace_back(
        ash::cros_healthd::mojom::PhysicalCpuInfo::New());
    cpu_info_ptr->physical_cpus = std::move(physical_cpus);

    auto& physical_cpus_ptr = cpu_info_ptr->physical_cpus;
    physical_cpus_ptr.front()->model_name = "fake-model-name";

    CrosHealthd::ParseCpuResult(&telemetry_info_ptr, &telemetry_info);
    EXPECT_EQ("fake-model-name",
              telemetry_info.cpu_info.physical_cpus.front().model_name);
  }
}

TEST(CrosHealthdTest, ParseBusResultCheckMissingBusResult) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  EXPECT_TRUE(telemetry_info.bus_devices.empty());
}

TEST(CrosHealthdTest, ParseBusResultCheckMissingBusInfo) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  telemetry_info_ptr->bus_result =
      ash::cros_healthd::mojom::BusResult::NewBusDevices({});
  auto& bus_devices_ptr = telemetry_info_ptr->bus_result->get_bus_devices();

  bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());

  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  EXPECT_TRUE(telemetry_info.bus_devices.empty());
}

TEST(CrosHealthdTest, ParseBusResultCheckPciBusDefault) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  telemetry_info_ptr->bus_result =
      ash::cros_healthd::mojom::BusResult::NewBusDevices({});
  auto& bus_devices_ptr = telemetry_info_ptr->bus_result->get_bus_devices();

  bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
  auto& bus_device_ptr = bus_devices_ptr.back();
  // Create PCI bus info.
  bus_device_ptr->bus_info = ash::cros_healthd::mojom::BusInfo::NewPciBusInfo(
      ash::cros_healthd::mojom::PciBusInfo::New());

  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  EXPECT_FALSE(telemetry_info.bus_devices.empty());
}

TEST(CrosHealthdTest, ParseBusResultCheckPciBus) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  telemetry_info_ptr->bus_result =
      ash::cros_healthd::mojom::BusResult::NewBusDevices({});
  auto& bus_devices_ptr = telemetry_info_ptr->bus_result->get_bus_devices();

  bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
  auto& bus_device_ptr = bus_devices_ptr.back();

  // Create PCI bus info.
  uint8_t class_id = 1;
  uint8_t subclass_id = 2;
  uint8_t protocol_id = 3;
  uint16_t vendor_id = 4;
  uint16_t device_id = 5;
  // Don't use driver after std::move.
  std::optional<std::string> driver = "some-driver";
  bus_device_ptr->bus_info = ash::cros_healthd::mojom::BusInfo::NewPciBusInfo(
      ash::cros_healthd::mojom::PciBusInfo::New(class_id, subclass_id,
                                                protocol_id, vendor_id,
                                                device_id, std::move(driver)));

  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  ASSERT_EQ(telemetry_info.bus_devices.size(), 1);

  const auto* pci_bus_info = std::get_if<TelemetryInfo::BusDevice::PciBusInfo>(
      &telemetry_info.bus_devices[0].bus_type_info);
  ASSERT_NE(pci_bus_info, nullptr);
  EXPECT_EQ(pci_bus_info->vendor_id, vendor_id);
  EXPECT_EQ(pci_bus_info->device_id, device_id);
  EXPECT_EQ(pci_bus_info->driver, "some-driver");
}

TEST(CrosHealthdTest, ParseBusResultCheckUsbBusDefault) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  telemetry_info_ptr->bus_result =
      ash::cros_healthd::mojom::BusResult::NewBusDevices({});
  auto& bus_devices_ptr = telemetry_info_ptr->bus_result->get_bus_devices();

  bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
  auto& bus_device_ptr = bus_devices_ptr.back();
  // Create USB bus info.
  bus_device_ptr->bus_info = ash::cros_healthd::mojom::BusInfo::NewUsbBusInfo(
      ash::cros_healthd::mojom::UsbBusInfo::New());

  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  EXPECT_FALSE(telemetry_info.bus_devices.empty());
}

TEST(CrosHealthdTest, ParseBusResultCheckUsbBus) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  telemetry_info_ptr->bus_result =
      ash::cros_healthd::mojom::BusResult::NewBusDevices({});
  auto& bus_devices_ptr = telemetry_info_ptr->bus_result->get_bus_devices();

  bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
  auto& bus_device_ptr = bus_devices_ptr.back();

  // Create USB bus info.
  uint8_t class_id = 1;
  uint8_t subclass_id = 2;
  uint8_t protocol_id = 3;
  uint16_t vendor_id = 4;
  uint16_t product_id = 5;
  // Don't use interface after std::move.
  std::vector<ash::cros_healthd::mojom::UsbBusInterfaceInfoPtr> interfaces;
  bus_device_ptr->bus_info = ash::cros_healthd::mojom::BusInfo::NewUsbBusInfo(
      ash::cros_healthd::mojom::UsbBusInfo::New(
          class_id, subclass_id, protocol_id, vendor_id, product_id,
          std::move(interfaces)));

  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  ASSERT_EQ(telemetry_info.bus_devices.size(), 1);

  const auto* usb_bus_info = std::get_if<TelemetryInfo::BusDevice::UsbBusInfo>(
      &telemetry_info.bus_devices[0].bus_type_info);
  ASSERT_NE(usb_bus_info, nullptr);
  EXPECT_EQ(usb_bus_info->vendor_id, vendor_id);
  EXPECT_EQ(usb_bus_info->product_id, product_id);
}

TEST(CrosHealthdTest, ParseBusResultCheckThunderboltBusDefault) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  telemetry_info_ptr->bus_result =
      ash::cros_healthd::mojom::BusResult::NewBusDevices({});
  auto& bus_devices_ptr = telemetry_info_ptr->bus_result->get_bus_devices();

  bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
  auto& bus_device_ptr = bus_devices_ptr.back();
  // Create Thunderbolt bus info.
  bus_device_ptr->bus_info =
      ash::cros_healthd::mojom::BusInfo::NewThunderboltBusInfo(
          ash::cros_healthd::mojom::ThunderboltBusInfo::New());

  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  // Thunderbolt is not parsed yet.
  EXPECT_TRUE(telemetry_info.bus_devices.empty());
}

TEST(CrosHealthdTest, ParseBusResultCheckThunderboltBus) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  telemetry_info_ptr->bus_result =
      ash::cros_healthd::mojom::BusResult::NewBusDevices({});
  auto& bus_devices_ptr = telemetry_info_ptr->bus_result->get_bus_devices();

  bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
  auto& bus_device_ptr = bus_devices_ptr.back();
  // Create Thunderbolt bus info.
  bus_device_ptr->bus_info =
      ash::cros_healthd::mojom::BusInfo::NewThunderboltBusInfo(
          ash::cros_healthd::mojom::ThunderboltBusInfo::New());

  auto& thunderbolt_bus_info_ptr =
      bus_device_ptr->bus_info->get_thunderbolt_bus_info();
  thunderbolt_bus_info_ptr->security_level =
      ash::cros_healthd::mojom::ThunderboltSecurityLevel::kNone;
  thunderbolt_bus_info_ptr->thunderbolt_interfaces.emplace_back(
      ash::cros_healthd::mojom::ThunderboltBusInterfaceInfo::New());

  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  // Thunderbolt is not parsed yet.
  EXPECT_TRUE(telemetry_info.bus_devices.empty());
}

TEST(CrosHealthdTest, ParseBusResultCheckAllBus) {
  TelemetryInfo telemetry_info{};
  auto&& telemetry_info_ptr = ash::cros_healthd::mojom::TelemetryInfo::New();
  telemetry_info_ptr->bus_result =
      ash::cros_healthd::mojom::BusResult::NewBusDevices({});
  auto& bus_devices_ptr = telemetry_info_ptr->bus_result->get_bus_devices();

  // Common usage across creating bus.
  uint8_t class_id = 1;
  uint8_t subclass_id = 2;
  uint8_t protocol_id = 3;
  uint16_t vendor_id = 4;

  // Create PCI bus info.
  uint16_t device_id = 7;
  {
    bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
    auto& bus_device_ptr = bus_devices_ptr.back();
    // Don't use driver after std::move.
    std::optional<std::string> driver = "some-driver";
    bus_device_ptr->bus_info = ash::cros_healthd::mojom::BusInfo::NewPciBusInfo(
        ash::cros_healthd::mojom::PciBusInfo::New(
            class_id, subclass_id, protocol_id, vendor_id, device_id,
            std::move(driver)));
  }

  // Create USB bus info.
  uint16_t product_id = 8;
  {
    bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
    auto& bus_device_ptr = bus_devices_ptr.back();
    // Don't use interface after std::move.
    std::vector<ash::cros_healthd::mojom::UsbBusInterfaceInfoPtr> interfaces;
    bus_device_ptr->bus_info = ash::cros_healthd::mojom::BusInfo::NewUsbBusInfo(
        ash::cros_healthd::mojom::UsbBusInfo::New(
            class_id, subclass_id, protocol_id, vendor_id, product_id,
            std::move(interfaces)));
  }

  // Create Thunderbolt bus info.
  {
    bus_devices_ptr.emplace_back(ash::cros_healthd::mojom::BusDevice::New());
    auto& bus_device_ptr = bus_devices_ptr.back();
    bus_device_ptr->bus_info =
        ash::cros_healthd::mojom::BusInfo::NewThunderboltBusInfo(
            ash::cros_healthd::mojom::ThunderboltBusInfo::New());
    auto& thunderbolt_bus_info_ptr =
        bus_device_ptr->bus_info->get_thunderbolt_bus_info();
    thunderbolt_bus_info_ptr->security_level =
        ash::cros_healthd::mojom::ThunderboltSecurityLevel::kNone;
    thunderbolt_bus_info_ptr->thunderbolt_interfaces.emplace_back(
        ash::cros_healthd::mojom::ThunderboltBusInterfaceInfo::New());
  }

  CrosHealthd::ParseBusResult(&telemetry_info_ptr, &telemetry_info);
  ASSERT_EQ(telemetry_info.bus_devices.size(), 2);

  // Check PCI bus info.
  const auto* pci_bus_info = std::get_if<TelemetryInfo::BusDevice::PciBusInfo>(
      &telemetry_info.bus_devices[0].bus_type_info);
  ASSERT_NE(pci_bus_info, nullptr);
  EXPECT_EQ(pci_bus_info->vendor_id, vendor_id);
  EXPECT_EQ(pci_bus_info->device_id, device_id);
  EXPECT_EQ(pci_bus_info->driver, "some-driver");
  // Check USB bus info.
  const auto* usb_bus_info = std::get_if<TelemetryInfo::BusDevice::UsbBusInfo>(
      &telemetry_info.bus_devices[1].bus_type_info);
  ASSERT_NE(usb_bus_info, nullptr);
  EXPECT_EQ(usb_bus_info->vendor_id, vendor_id);
  EXPECT_EQ(usb_bus_info->product_id, product_id);
  // Thunderbolt is not parsed yet.
}

}  // namespace chromeos_update_engine
