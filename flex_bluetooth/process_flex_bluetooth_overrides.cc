// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <brillo/udev/udev_enumerate.h>

#include "flex_bluetooth/flex_bluetooth_overrides.h"

namespace {

const char kAttributeDeviceClass[] = "bDeviceClass";
const char kAttributeDeviceSubClass[] = "bDeviceSubClass";
const char kAttributeIdProduct[] = "idProduct";
const char kAttributeIdVendor[] = "idVendor";
// The below DeviceClass and DeviceSubClass can be found at
// https://www.usb.org/defined-class-codes
const char kBluetoothDeviceClass[] = "e0";
const char kBluetoothDeviceSubClass[] = "01";

const base::FilePath kSyspropOverridePath = base::FilePath(
    "/var/lib/bluetooth/sysprops.conf.d/floss_reven_overrides.conf");

const std::map<flex_bluetooth::BluetoothAdapter,
               std::unordered_set<flex_bluetooth::SyspropOverride>>
    kAdapterSyspropOverrides = {
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe0a2},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x04ca, 0x3015},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe007},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe009},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe300},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe500},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3491},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3519},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3496},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3501},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x8086, 0x0189},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0a12, 0x0001},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x3004},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x07da},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0a2a},
         {flex_bluetooth::SyspropOverride::kDisableEnhancedSCOConnection}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0a2b},
         {flex_bluetooth::SyspropOverride::kDisableEnhancedSCOConnection}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0aa7},
         {flex_bluetooth::SyspropOverride::kDisableEnhancedSCOConnection}},

        // Enable MSFT AdvMon quirk on RTL8852BE.
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3570},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3571},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3572},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3591},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe123},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe125},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},

        // Disable packet boundary & sniff mode opcode for qca chips
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x311e},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe04e},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x311e},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe04e},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x817b},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe04e},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x04c5, 0x1330},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x817b},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe04e},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},
        {flex_bluetooth::BluetoothAdapter{0x04c5, 0x1330},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},

        // Disable packet boundary for Intel AC7265 chips
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0a2a},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0a2b},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0aa7},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},

        // Resolve crashes from b/408887245
        {flex_bluetooth::BluetoothAdapter{0x04ca, 0x3016},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3496},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3501},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3503},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x07dc},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x3641, 0x0902},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe09f},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0a5c, 0x216d},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},

        // Resolve crashes from b/401624875
        {flex_bluetooth::BluetoothAdapter{0x413c, 0x8140},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x044e, 0x301d},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x05ac, 0x8205},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
};
}  // namespace

int main() {
  brillo::InitLog(brillo::kLogToSyslog);
  LOG(INFO) << "Started process_flex_bluetooth_overrides.";

  const auto udev = brillo::Udev::Create();
  const auto enumerate = udev->CreateEnumerate();
  if (!enumerate->AddMatchSysAttribute(kAttributeDeviceClass,
                                       kBluetoothDeviceClass) ||
      !enumerate->AddMatchSysAttribute(kAttributeDeviceSubClass,
                                       kBluetoothDeviceSubClass) ||
      !enumerate->ScanDevices()) {
    LOG(INFO) << "No Bluetooth adapter found. Exiting.";
    return 0;
  }

  const flex_bluetooth::FlexBluetoothOverrides bt(kSyspropOverridePath,
                                                  kAdapterSyspropOverrides);
  bool found_bt_adapter = false;
  uint16_t id_vendor;
  uint16_t id_product;
  for (std::unique_ptr<brillo::UdevListEntry> list_entry =
           enumerate->GetListEntry();
       list_entry; list_entry = list_entry->GetNext()) {
    const std::string sys_path = list_entry->GetName() ?: "";
    const std::unique_ptr<brillo::UdevDevice> device =
        udev->CreateDeviceFromSysPath(sys_path.c_str());
    if (!device) {
      continue;
    }

    const std::string vendor =
        device->GetSysAttributeValue(kAttributeIdVendor) ?: "";
    const std::string product =
        device->GetSysAttributeValue(kAttributeIdProduct) ?: "";

    LOG(INFO) << "Found Bluetooth adapter with idVendor: " << vendor
              << " and idProduct: " << product;

    if (!flex_bluetooth::HexStringToUInt16(vendor, &id_vendor)) {
      LOG(WARNING) << "Unable to convert vendor " << vendor << " to uint16_t.";
      continue;
    }

    if (!flex_bluetooth::HexStringToUInt16(product, &id_product)) {
      LOG(WARNING) << "Unable to convert product " << product
                   << " to uint16_t.";
      continue;
    }

    found_bt_adapter = true;
    bt.ProcessOverridesForVidPid(id_vendor, id_product);

    // TODO(b/277581437): Handle the case when there are multiple Bluetooth
    // adapters. There's currently only support for one Bluetooth adapter.
    // This presents issue where an external Bluetooth adapter cannot be
    // used over an existing internal Bluetooth adapter.
    // (To clarify, if a device has no internal Bluetooth adapter, a user can
    // still currently use an external Bluetooth adapter since there is only
    // one Bluetooth adapter to choose from).
    break;
  }

  if (!found_bt_adapter) {
    LOG(INFO) << "Didn't find a Bluetooth adapter. Removing overrides.";
    bt.RemoveOverrides();
  }

  LOG(INFO) << "Exiting process_flex_bluetooth_overrides.";
  return 0;
}
