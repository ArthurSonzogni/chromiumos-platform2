// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/telemetry_info.h"

#include <gtest/gtest.h>

namespace chromeos_update_engine {

class TelemetryInfoTest : public ::testing::Test {
 protected:
  TelemetryInfo telemetry_info;
};

TEST_F(TelemetryInfoTest, GetWirelessDrivers) {
  telemetry_info.bus_devices = {
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kWirelessController,
          .bus_type_info =
              TelemetryInfo::BusDevice::PciBusInfo{
                  .driver = "fake-driver-1",
              },
      },
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kWirelessController,
          .bus_type_info =
              TelemetryInfo::BusDevice::PciBusInfo{
                  .driver = "fake-driver-2",
              },
      },
      // Should ignore USB bus type.
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kWirelessController,
          .bus_type_info = TelemetryInfo::BusDevice::UsbBusInfo{},
      },
      // Should ignore non wireless controller.
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kDisplayController,
          .bus_type_info =
              TelemetryInfo::BusDevice::PciBusInfo{
                  .driver = "should-not-be-included",
              },
      },
  };
  EXPECT_EQ("fake-driver-1 fake-driver-2", telemetry_info.GetWirelessDrivers());
}

TEST_F(TelemetryInfoTest, GetWirelessIds) {
  telemetry_info.bus_devices = {
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kWirelessController,
          .bus_type_info =
              TelemetryInfo::BusDevice::PciBusInfo{
                  .vendor_id = 0x0001,
                  .device_id = 0x0002,
              },
      },
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kWirelessController,
          .bus_type_info =
              TelemetryInfo::BusDevice::PciBusInfo{
                  .vendor_id = 0x0003,
                  .device_id = 0x0004,
              },
      },
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kWirelessController,
          .bus_type_info =
              TelemetryInfo::BusDevice::UsbBusInfo{
                  .vendor_id = 0x0005,
                  .product_id = 0x0006,
              },
      },
      // Should ignore non wireless controller.
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kDisplayController,
          .bus_type_info =
              TelemetryInfo::BusDevice::PciBusInfo{
                  .vendor_id = 0x0007,
                  .device_id = 0x0008,
              },
      },
  };
  EXPECT_EQ("0001:0002 0003:0004 0005:0006", telemetry_info.GetWirelessIds());
}

TEST_F(TelemetryInfoTest, GetGpuIds) {
  telemetry_info.bus_devices = {
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kDisplayController,
          .bus_type_info =
              TelemetryInfo::BusDevice::PciBusInfo{
                  .vendor_id = 0x8086,
                  .device_id = 0x0002,
              },
      },
      // Should ignore non display controller.
      {
          .device_class =
              TelemetryInfo::BusDevice::BusDeviceClass::kWirelessController,
          .bus_type_info =
              TelemetryInfo::BusDevice::PciBusInfo{
                  .vendor_id = 0x0003,
                  .device_id = 0x0004,
              },
      },
  };
  EXPECT_EQ("8086:0002", telemetry_info.GetGpuIds());
}

}  // namespace chromeos_update_engine
