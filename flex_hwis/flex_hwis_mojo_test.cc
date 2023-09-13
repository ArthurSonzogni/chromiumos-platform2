// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_mojo.h"
#include "flex_hwis/hwis_data.pb.h"
#include "flex_hwis/telemetry_for_testing.h"

#include <optional>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom-shared.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/service_constants.h>

namespace flex_hwis {
namespace mojom = ::ash::cros_healthd::mojom;

class FlexHwisMojoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(test_dir_.CreateUniqueTempDir());
    flex_hwis_mojo_ = flex_hwis::FlexHwisMojo();
  }

  std::optional<flex_hwis::FlexHwisMojo> flex_hwis_mojo_;
  base::ScopedTempDir test_dir_;
  flex_hwis::TelemetryForTesting telemetry_;
};

TEST_F(FlexHwisMojoTest, MojoSystem) {
  hwis_proto::Device data;
  telemetry_.AddSystemInfo();
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetSystemInfo(&data);

  EXPECT_EQ(data.mutable_dmi_info()->vendor(), kSystemVersion);
  EXPECT_EQ(data.mutable_dmi_info()->product_name(), kSystemProductName);
  EXPECT_EQ(data.mutable_dmi_info()->product_version(), kSystemProductVersion);
  EXPECT_EQ(data.mutable_bios()->bios_version(), kSystemBiosVersion);
  EXPECT_EQ(data.mutable_bios()->uefi(), kSystemUefi);
}

TEST_F(FlexHwisMojoTest, MojoCpu) {
  hwis_proto::Device data;
  telemetry_.AddCpuInfo();
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetCpuInfo(&data);

  EXPECT_EQ(data.cpu(0).name(), kCpuModelName);
}

TEST_F(FlexHwisMojoTest, MojoMemory) {
  hwis_proto::Device data;
  telemetry_.AddMemoryInfo();
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetMemoryInfo(&data);

  EXPECT_EQ(data.mutable_memory()->total_kib(), kMemoryKib);
}

TEST_F(FlexHwisMojoTest, MojoBusEthernet) {
  hwis_proto::Device data;
  telemetry_.AddPciBusInfo(mojom::BusDeviceClass::kEthernetController);
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetBusInfo(&data);

  EXPECT_EQ(data.ethernet_adapter(0).name(), kBusPciName);
  EXPECT_EQ(data.ethernet_adapter(0).id(), kPciId);
  EXPECT_EQ(data.ethernet_adapter(0).driver(0), kDriver);
  EXPECT_EQ(data.ethernet_adapter(0).bus(), hwis_proto::Device::PCI);
}

TEST_F(FlexHwisMojoTest, MojoBusWireless) {
  hwis_proto::Device data;
  telemetry_.AddPciBusInfo(mojom::BusDeviceClass::kWirelessController);
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetBusInfo(&data);

  EXPECT_EQ(data.wireless_adapter(0).name(), kBusPciName);
  EXPECT_EQ(data.wireless_adapter(0).id(), kPciId);
  EXPECT_EQ(data.wireless_adapter(0).driver(0), kDriver);
  EXPECT_EQ(data.wireless_adapter(0).bus(), hwis_proto::Device::PCI);
}

TEST_F(FlexHwisMojoTest, MojoBusBluetooth) {
  hwis_proto::Device data;
  telemetry_.AddUsbBusInfo(mojom::BusDeviceClass::kBluetoothAdapter);
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetBusInfo(&data);

  EXPECT_EQ(data.bluetooth_adapter(0).name(), kBusUsbName);
  EXPECT_EQ(data.bluetooth_adapter(0).id(), kUsbId);
  EXPECT_EQ(data.bluetooth_adapter(0).driver(0), kDriver);
  EXPECT_EQ(data.bluetooth_adapter(0).bus(), hwis_proto::Device::USB);
}

TEST_F(FlexHwisMojoTest, MojoBusDisplay) {
  hwis_proto::Device data;
  // Some flex devices have multiple graphics adapters.
  telemetry_.AddPciBusInfo(mojom::BusDeviceClass::kDisplayController,
                           kPciVendorName, kBusProductName, kPciBusVendorId,
                           kPciBusDeviceId, kPciBusDriver);
  telemetry_.AddPciBusInfo(mojom::BusDeviceClass::kDisplayController,
                           kPciVendorName, kBusProductName,
                           kSecondPciBusVendorId, kSecondPciBusDeviceId,
                           kPciBusDriver);
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetBusInfo(&data);

  EXPECT_EQ(data.gpu(0).name(), kBusPciName);
  EXPECT_EQ(data.gpu(0).id(), kPciId);
  EXPECT_EQ(data.gpu(0).driver(0), kDriver);
  EXPECT_EQ(data.gpu(0).bus(), hwis_proto::Device::PCI);
  EXPECT_EQ(data.gpu(1).id(), kSecondPciId);
}

TEST_F(FlexHwisMojoTest, MojoGraphics) {
  hwis_proto::Device data;
  telemetry_.AddGraphicsInfo();
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetGraphicInfo(&data);

  EXPECT_EQ(data.mutable_graphics_info()->gl_version(), kGraphicsVersion);
  EXPECT_EQ(data.mutable_graphics_info()->gl_shading_version(),
            kGraphicsShadingVer);
  EXPECT_EQ(data.mutable_graphics_info()->gl_vendor(), kGraphicsVendor);
  EXPECT_EQ(data.mutable_graphics_info()->gl_renderer(), kGraphicsRenderer);
  EXPECT_EQ(data.mutable_graphics_info()->gl_extensions(0), kGraphicsExtension);
}

TEST_F(FlexHwisMojoTest, MojoInput) {
  hwis_proto::Device data;
  telemetry_.AddInputInfo();
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetInputInfo(&data);

  EXPECT_EQ(data.mutable_touchpad()->stack(), kTouchpadLibraryName);
}

TEST_F(FlexHwisMojoTest, MojoTpm) {
  hwis_proto::Device data;
  telemetry_.AddTpmInfo();
  flex_hwis_mojo_->SetTelemetryInfoForTesting(telemetry_.Get());
  flex_hwis_mojo_->SetTpmInfo(&data);

  EXPECT_EQ(data.mutable_tpm()->tpm_version(), kTpmFamilyStr);
  EXPECT_EQ(data.mutable_tpm()->spec_level(), kTpmSpecLevel);
  EXPECT_EQ(data.mutable_tpm()->manufacturer(), kTpmManufacturer);
  EXPECT_EQ(data.mutable_tpm()->did_vid(), kTpmDidVid);
  EXPECT_EQ(data.mutable_tpm()->tpm_allow_listed(), kTpmIsAllowed);
  EXPECT_EQ(data.mutable_tpm()->tpm_owned(), kTpmOwned);
}

}  // namespace flex_hwis
