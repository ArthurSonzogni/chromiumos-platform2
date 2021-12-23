// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/component_utils.h"

#include <iomanip>
#include <sstream>
#include <string>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace {

std::string Uint32ToHexString(uint32_t v, int width) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(width) << v;
  return ss.str();
}

}  // namespace

namespace rmad {

// Implementation for each component fields defined by runtime_probe.
// See platform2/system_api/dbus/runtime_probe/runtime_probe.proto for type and
// bit length for each fields.
std::string GetComponentFieldsIdentifier(
    const runtime_probe::Battery_Fields& fields) {
  // Battery. Identifier is "Battery_<manufacturer name>_<model name>".
  return "Battery_" + fields.manufacturer() + "_" + fields.model_name();
}

std::string GetComponentFieldsIdentifier(
    const runtime_probe::Storage_Fields& fields) {
  // Storage. Identifier depends on storage type.
  if (fields.type() == "MMC") {
    // eMMC storage. Identifier is "Storage(eMMC)_<manufacturer id>_<name>".
    return "Storage(eMMC)_" + Uint32ToHexString(fields.mmc_manfid(), 2) + "_" +
           fields.mmc_name();
  } else if (fields.type() == "NVMe") {
    // NVMe storage. Identifier is "Storage(NVMe)_<vendor id>_<device id>".
    return "Storage(NVMe)_" + Uint32ToHexString(fields.pci_vendor(), 4) + "_" +
           Uint32ToHexString(fields.pci_device(), 4);
  } else if (fields.type() == "ATA") {
    // SATA storage. Identifier is
    // "Storage(SATA)_<vendor name>_<model name>".
    return "Storage(SATA)_" + fields.ata_vendor() + "_" + fields.ata_model();
  }
  return "Storage(unknown)";
}

std::string GetComponentFieldsIdentifier(
    const runtime_probe::Camera_Fields& fields) {
  // Camera. Identifier is "Camera_<vendor id>_<product id>".
  return "Camera_" + Uint32ToHexString(fields.usb_vendor_id(), 4) + "_" +
         Uint32ToHexString(fields.usb_product_id(), 4);
}

std::string GetComponentFieldsIdentifier(
    const runtime_probe::InputDevice_Fields& fields) {
  // Input device. Identifier is "<type>_<vendor id>_<product_id>".
  if (fields.device_type() == runtime_probe::InputDevice::TYPE_STYLUS) {
    return "Stylus_" + Uint32ToHexString(fields.vendor(), 4) + "_" +
           Uint32ToHexString(fields.product(), 4);
  } else if (fields.device_type() ==
             runtime_probe::InputDevice::TYPE_TOUCHPAD) {
    return "Touchpad_" + Uint32ToHexString(fields.vendor(), 4) + "_" +
           Uint32ToHexString(fields.product(), 4);
  } else if (fields.device_type() ==
             runtime_probe::InputDevice::TYPE_TOUCHSCREEN) {
    return "Touchscreen_" + Uint32ToHexString(fields.vendor(), 4) + "_" +
           Uint32ToHexString(fields.product(), 4);
  }
  return "InputDevice(unknown)";
}

std::string GetComponentFieldsIdentifier(
    const runtime_probe::Memory_Fields& fields) {
  // Memory. Identifier is "Memory_<part number>".
  return "Memory_" + fields.part();
}

std::string GetComponentFieldsIdentifier(
    const runtime_probe::Edid_Fields& fields) {
  // Display panel. Identifier is "Display_<vendor code>_<product_id>".
  return "Display_" + fields.vendor() + "_" +
         Uint32ToHexString(fields.product_id(), 4);
}

std::string GetComponentFieldsIdentifier(
    const runtime_probe::Network_Fields& fields) {
  // Network (wireless/ethernet/cellular). Identifier depends on bus type.
  if (fields.bus_type() == "pci") {
    // PCI. Identifier is "Network(<type>:pci)_<vendor id>_<device_id>".
    return "Network(" + fields.type() + ":pci)_" +
           Uint32ToHexString(fields.pci_vendor_id(), 4) + "_" +
           Uint32ToHexString(fields.pci_device_id(), 4);
  } else if (fields.bus_type() == "usb") {
    // USB. Identifier is "Network(<type>:usb)_<vendor id>_<product_id>".
    return "Network(" + fields.type() + ":usb)_" +
           Uint32ToHexString(fields.usb_vendor_id(), 4) + "_" +
           Uint32ToHexString(fields.usb_product_id(), 4);
  } else if (fields.bus_type() == "sdio") {
    // SDIO. |identifier| is "Network(<type>:sdio)_<vendor id>_<device_id>".
    return "Network(" + fields.type() + ":sdio)_" +
           Uint32ToHexString(fields.sdio_vendor_id(), 4) + "_" +
           Uint32ToHexString(fields.sdio_device_id(), 4);
  }
  return "Network(" + fields.type() + ":unknown)";
}

// Extension for |runtime_probe::ComponentFields|.
std::string GetComponentFieldsIdentifier(
    const runtime_probe::ComponentFields& component_fields) {
  if (component_fields.has_battery()) {
    return GetComponentFieldsIdentifier(component_fields.battery());
  } else if (component_fields.has_storage()) {
    return GetComponentFieldsIdentifier(component_fields.storage());
  } else if (component_fields.has_camera()) {
    return GetComponentFieldsIdentifier(component_fields.camera());
  } else if (component_fields.has_stylus()) {
    return GetComponentFieldsIdentifier(component_fields.stylus());
  } else if (component_fields.has_touchpad()) {
    return GetComponentFieldsIdentifier(component_fields.touchpad());
  } else if (component_fields.has_touchscreen()) {
    return GetComponentFieldsIdentifier(component_fields.touchscreen());
  } else if (component_fields.has_dram()) {
    return GetComponentFieldsIdentifier(component_fields.dram());
  } else if (component_fields.has_display_panel()) {
    return GetComponentFieldsIdentifier(component_fields.display_panel());
  } else if (component_fields.has_cellular()) {
    return GetComponentFieldsIdentifier(component_fields.cellular());
  } else if (component_fields.has_ethernet()) {
    return GetComponentFieldsIdentifier(component_fields.ethernet());
  } else if (component_fields.has_wireless()) {
    return GetComponentFieldsIdentifier(component_fields.wireless());
  }
  return "UnknownComponent";
}

}  // namespace rmad
