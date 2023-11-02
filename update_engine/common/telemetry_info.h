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

#ifndef UPDATE_ENGINE_COMMON_TELEMETRY_INFO_H_
#define UPDATE_ENGINE_COMMON_TELEMETRY_INFO_H_

#include <string>
#include <variant>
#include <vector>

namespace chromeos_update_engine {

enum class TelemetryCategoryEnum {
  kBattery = 0,
  kNonRemovableBlockDevices = 1,
  kCpu = 2,
  kTimezone = 3,
  kMemory = 4,
  kBacklight = 5,
  kFan = 6,
  kStatefulPartition = 7,
  kBluetooth = 8,
  kSystem = 9,
  kNetwork = 10,
  kAudio = 11,
  kBootPerformance = 12,
  kBus = 13,
};

// Represents the telemetry information collected from `cros_healthd`.
typedef struct TelemetryInfo {
  typedef struct SystemInfo {
    typedef struct DmiInfo {
      std::string sys_vendor;
      std::string product_name;
      std::string product_version;
      std::string bios_version;
    } DmiInfo;
    DmiInfo dmi_info;

    typedef struct OsInfo {
      enum class BootMode : int32_t {
        kUnknown = 0,
        kCrosSecure = 1,
        kCrosEfi = 2,
        kCrosLegacy = 3,
      };
      BootMode boot_mode;
    } OsInfo;
    OsInfo os_info;
  } SystemInfo;
  SystemInfo system_info;

  typedef struct MemoryInfo {
    uint32_t total_memory_kib;
  } MemoryInfo;
  MemoryInfo memory_info;

  typedef struct NonRemovableBlockDeviceInfo {
    uint64_t size;
  } NonRemovableBlockDeviceInfo;
  std::vector<NonRemovableBlockDeviceInfo> block_device_info;

  typedef struct CpuInfo {
    typedef struct PhysicalCpuInfo {
      std::string model_name;
    } PhysicalCpuInfo;
    std::vector<PhysicalCpuInfo> physical_cpus;
  } CpuInfo;
  CpuInfo cpu_info;

  typedef struct BusDevice {
    enum class BusDeviceClass : int32_t {
      kOthers = 0,
      kDisplayController = 1,
      kEthernetController = 2,
      kWirelessController = 3,
      kBluetoothAdapter = 4,
    };
    typedef struct PciBusInfo {
      uint16_t vendor_id;
      uint16_t device_id;
      std::string driver;
    } PciBusInfo;
    typedef struct UsbBusInfo {
      uint16_t vendor_id;
      uint16_t product_id;
    } UsbBusInfo;

    BusDeviceClass device_class;
    std::variant<PciBusInfo, UsbBusInfo> bus_type_info;
  } BusDevice;
  std::vector<BusDevice> bus_devices;

  std::string GetWirelessDrivers() const;
  std::string GetWirelessIds() const;
  std::string GetGpuDrivers() const;
  std::string GetGpuIds() const;

 private:
  std::string GetBusDeviceDrivers(
      BusDevice::BusDeviceClass bus_device_class) const;
  std::string GetBusDeviceIds(BusDevice::BusDeviceClass bus_device_class) const;
} TelemetryInfo;

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_TELEMETRY_INFO_H_
