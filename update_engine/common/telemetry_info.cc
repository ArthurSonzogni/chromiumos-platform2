// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/telemetry_info.h"

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

namespace chromeos_update_engine {

std::string TelemetryInfo::GetWirelessDrivers() const {
  return GetBusDeviceDrivers(BusDevice::BusDeviceClass::kWirelessController);
}

std::string TelemetryInfo::GetWirelessIds() const {
  return GetBusDeviceIds(BusDevice::BusDeviceClass::kWirelessController);
}

std::string TelemetryInfo::GetGpuDrivers() const {
  return GetBusDeviceDrivers(BusDevice::BusDeviceClass::kDisplayController);
}

std::string TelemetryInfo::GetGpuIds() const {
  return GetBusDeviceIds(BusDevice::BusDeviceClass::kDisplayController);
}

std::string TelemetryInfo::GetBusDeviceDrivers(
    BusDevice::BusDeviceClass bus_device_class) const {
  std::vector<std::string> drivers;
  for (const auto& bus_device : bus_devices) {
    if (bus_device.device_class != bus_device_class) {
      continue;
    }
    if (const auto* pci_bus_info =
            std::get_if<BusDevice::PciBusInfo>(&bus_device.bus_type_info)) {
      const auto& driver = pci_bus_info->driver;
      if (!driver.empty()) {
        drivers.push_back(driver);
      }
    }
  }
  return base::JoinString(drivers, " ");
}

std::string TelemetryInfo::GetBusDeviceIds(
    BusDevice::BusDeviceClass bus_device_class) const {
  std::vector<std::string> ids;
  for (const auto& bus_device : bus_devices) {
    if (bus_device.device_class != bus_device_class) {
      continue;
    }
    if (const auto* pci_bus_info =
            std::get_if<BusDevice::PciBusInfo>(&bus_device.bus_type_info)) {
      ids.push_back(base::JoinString(
          {
              base::StringPrintf("%04X", pci_bus_info->vendor_id),
              base::StringPrintf("%04X", pci_bus_info->device_id),
          },
          ":"));
    } else if (const auto* usb_bus_info = std::get_if<BusDevice::UsbBusInfo>(
                   &bus_device.bus_type_info)) {
      ids.push_back(base::JoinString(
          {
              base::StringPrintf("%04X", usb_bus_info->vendor_id),
              base::StringPrintf("%04X", usb_bus_info->product_id),
          },
          ":"));
    }
  }
  return base::JoinString(ids, " ");
}

}  // namespace chromeos_update_engine
