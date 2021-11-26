// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/component_utils.h"

#include <iomanip>
#include <sstream>
#include <string>

#include <hardware_verifier/hardware_verifier.pb.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace {

std::string Uint32ToHexString(uint32_t v, int width) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(width) << v;
  return ss.str();
}

void AppendNetworkIdentifier(std::ostringstream* identifier,
                             const runtime_probe::Network::Fields& fields) {
  // Network (wireless/ethernet/cellular). |identifier| depends on bus type.
  if (fields.bus_type() == "pci") {
    // PCI. |identifier| is "Network(<type>:pci)_<vendor id>_<device_id>".
    *identifier << "Network(" << fields.type() << ":pci)_"
                << Uint32ToHexString(fields.pci_vendor_id(), 4) << "_"
                << Uint32ToHexString(fields.pci_device_id(), 4);
  } else if (fields.bus_type() == "usb") {
    // USB. |identifier| is "Network(<type>:usb)_<vendor id>_<product_id>".
    *identifier << "Network(" << fields.type() << ":usb)_" << std::hex
                << Uint32ToHexString(fields.usb_vendor_id(), 4) << "_"
                << Uint32ToHexString(fields.usb_product_id(), 4);
  } else if (fields.bus_type() == "sdio") {
    // SDIO. |identifier| is "Network(<type>:sdio)_<vendor id>_<device_id>".
    *identifier << "Network(" << fields.type() << ":sdio)_" << std::hex
                << Uint32ToHexString(fields.sdio_vendor_id(), 4) << "_"
                << Uint32ToHexString(fields.sdio_device_id(), 4);
  } else {
    *identifier << "Network(" << fields.type() << ":unknown)";
  }
}

}  // namespace

namespace rmad {

// See platform2/system_api/dbus/runtime_probe/runtime_probe.proto for type and
// bit length for each fields.
std::string GetComponentIdentifier(
    const hardware_verifier::ComponentInfo& info) {
  std::ostringstream identifier;
  const runtime_probe::ComponentFields& component_fields =
      info.component_fields();
  if (component_fields.has_audio_codec()) {
    // Audio codec. |identifier| is "Audio_<name>".
    const runtime_probe::AudioCodec::Fields fields =
        component_fields.audio_codec();
    identifier << "Audio_" << fields.name();
  } else if (component_fields.has_battery()) {
    // Battery. |identifier| is "Battery_<manufacturer name>_<model name>".
    const runtime_probe::Battery::Fields& fields = component_fields.battery();
    identifier << "Battery_" << fields.manufacturer() << "_"
               << fields.model_name();
  } else if (component_fields.has_storage()) {
    // Storage. |identifier| depends on storage type.
    const runtime_probe::Storage::Fields& fields = component_fields.storage();
    if (component_fields.storage().type() == "MMC") {
      // eMMC storage. |identifier| is "Storage(eMMC)_<manufacturer id>_<name>".
      identifier << "Storage(eMMC)_"
                 << Uint32ToHexString(fields.mmc_manfid(), 2) << "_"
                 << fields.mmc_name();
    } else if (component_fields.storage().type() == "NVMe") {
      // NVMe storage. |identifier| is "Storage(NVMe)_<vendor id>_<device id>".
      identifier << "Storage(NVMe)_"
                 << Uint32ToHexString(fields.pci_vendor(), 4) << "_"
                 << Uint32ToHexString(fields.pci_device(), 4);
    } else if (component_fields.storage().type() == "ATA") {
      // SATA storage. |identifier| is
      // "Storage(SATA)_<vendor name>_<model name>".
      identifier << "Storage(SATA)_" << fields.ata_vendor() << "_"
                 << fields.ata_model();
    } else {
      identifier << "Storage(unknown)";
    }
  } else if (component_fields.has_camera()) {
    // Camera. |identifier| is "Camera_<vendor id>_<product id>".
    const runtime_probe::Camera::Fields& fields = component_fields.camera();
    identifier << "Camera_" << Uint32ToHexString(fields.usb_vendor_id(), 4)
               << "_" << Uint32ToHexString(fields.usb_product_id(), 4);
  } else if (component_fields.has_stylus()) {
    // Stylus. |identifier| is "Stylus_<vendor id>_<product_id>".
    const runtime_probe::InputDevice::Fields& fields =
        component_fields.stylus();
    identifier << "Stylus_" << Uint32ToHexString(fields.vendor(), 4) << "_"
               << Uint32ToHexString(fields.product(), 4);
  } else if (component_fields.has_touchpad()) {
    // Touchpad. |identifier| is "Touchpad_<vendor id>_<product_id>".
    const runtime_probe::InputDevice::Fields& fields =
        component_fields.touchpad();
    identifier << "Touchpad_" << Uint32ToHexString(fields.vendor(), 4) << "_"
               << Uint32ToHexString(fields.product(), 4);
  } else if (component_fields.has_touchscreen()) {
    // Touchscreen. |identifier| is "Touchscreen_<vendor id>_<product_id>".
    const runtime_probe::InputDevice::Fields& fields =
        component_fields.touchscreen();
    identifier << "Touchscreen_" << Uint32ToHexString(fields.vendor(), 4) << "_"
               << Uint32ToHexString(fields.product(), 4);
  } else if (component_fields.has_dram()) {
    // Memory. |identifier| is "Memory_<part number>".
    const runtime_probe::Memory::Fields& fields = component_fields.dram();
    identifier << "Memory_" << fields.part();
  } else if (component_fields.has_display_panel()) {
    // Display panel. |identifier| is "Display_<vendor code>_<product_id>".
    const runtime_probe::Edid::Fields& fields =
        component_fields.display_panel();
    identifier << "Display_" << fields.vendor() << "_"
               << Uint32ToHexString(fields.product_id(), 4);
  } else if (component_fields.has_cellular()) {
    const runtime_probe::Network::Fields& fields = component_fields.cellular();
    AppendNetworkIdentifier(&identifier, fields);
  } else if (component_fields.has_ethernet()) {
    const runtime_probe::Network::Fields& fields = component_fields.ethernet();
    AppendNetworkIdentifier(&identifier, fields);
  } else if (component_fields.has_wireless()) {
    const runtime_probe::Network::Fields& fields = component_fields.wireless();
    AppendNetworkIdentifier(&identifier, fields);
  } else {
    identifier << "UnknownComponent";
  }

  return identifier.str();
}

}  // namespace rmad
