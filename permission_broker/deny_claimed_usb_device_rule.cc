// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/deny_claimed_usb_device_rule.h"

#include <libudev.h>
#include <linux/usb/ch9.h>

#include <iterator>
#include <memory>
#include <string>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "permission_broker/udev_scopers.h"

#include <base/check.h>

using policy::DevicePolicy;

namespace {

const uint32_t kAdbClass = 0xff;
const uint32_t kAdbSubclass = 0x42;
const uint32_t kAdbProtocol = 0x1;

enum class RemovableAttr {
  kUnknown,
  kFixed,
  kRemovable,
};

RemovableAttr ParseRemovableSysattr(const std::string& removable) {
  if (removable == "fixed") {
    return RemovableAttr::kFixed;
  } else if (removable == "removable") {
    return RemovableAttr::kRemovable;
  } else {
    if (removable != "unknown") {
      LOG(WARNING) << "Unexpected value for removable sysattr: '" << removable
                   << "'";
    }
    return RemovableAttr::kUnknown;
  }
}

RemovableAttr GetRemovableSysattr(udev_device* device) {
  const char* removable = udev_device_get_sysattr_value(device, "removable");
  if (!removable) {
    return RemovableAttr::kUnknown;
  }
  return ParseRemovableSysattr(removable);
}

bool GetUIntSysattr(udev_device* device, const char* key, uint32_t* val) {
  CHECK(val);

  const char* str_val = udev_device_get_sysattr_value(device, key);
  return str_val && base::HexStringToUInt(str_val, val);
}

// Check if a USB vendor:product ID pair is in the provided list.
// Entries in the list with |product_id| of 0 match any product with the
// corresponding |vendor_id|.
template <typename Iterator>
bool UsbDeviceListContainsId(Iterator first,
                             Iterator last,
                             uint16_t vendor_id,
                             uint16_t product_id) {
  while (first != last) {
    if (first->vendor_id == vendor_id &&
        (!first->product_id || first->product_id == product_id))
      return true;
    ++first;
  }
  return false;
}

}  // namespace

namespace permission_broker {

DenyClaimedUsbDeviceRule::DenyClaimedUsbDeviceRule()
    : UsbSubsystemUdevRule("DenyClaimedUsbDeviceRule"), policy_loaded_(false) {}

DenyClaimedUsbDeviceRule::~DenyClaimedUsbDeviceRule() = default;

bool DenyClaimedUsbDeviceRule::LoadPolicy() {
  usb_allow_list_.clear();

  auto policy_provider = std::make_unique<policy::PolicyProvider>();
  policy_provider->Reload();

  // No available policies.
  if (!policy_provider->device_policy_is_loaded())
    return false;

  const policy::DevicePolicy* policy = &policy_provider->GetDevicePolicy();
  return policy->GetUsbDetachableWhitelist(&usb_allow_list_);
}

bool DenyClaimedUsbDeviceRule::IsDeviceDetachableByPolicy(udev_device* device) {
  // Retrieve the device policy for detachable USB devices if needed.
  if (!policy_loaded_)
    policy_loaded_ = LoadPolicy();
  if (!policy_loaded_)
    return false;

  // Check whether this USB device is allowed.
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  return UsbDeviceListContainsId(usb_allow_list_.begin(), usb_allow_list_.end(),
                                 vendor_id, product_id);
}

bool DenyClaimedUsbDeviceRule::IsInterfaceAdb(udev_device* device) {
  uint32_t intf_class, intf_subclass, intf_protocol;
  if (!GetUIntSysattr(device, "bInterfaceClass", &intf_class) ||
      !GetUIntSysattr(device, "bInterfaceSubClass", &intf_subclass) ||
      !GetUIntSysattr(device, "bInterfaceProtocol", &intf_protocol))
    return false;

  return intf_class == kAdbClass && intf_subclass == kAdbSubclass &&
         intf_protocol == kAdbProtocol;
}

bool IsInterfaceUsbSerial(udev_device* iface) {
  // Search all children of the interface in the 'usb-serial' subsystem
  // this includes all the USB-serial converter and most micro-controllers
  // USB bulk endpoints presenting a serial-like interface but not CDC-ACM
  // devices (e.g. modems or boards pretending to be one)
  udev* udev = udev_device_get_udev(iface);
  ScopedUdevEnumeratePtr enum_serial(udev_enumerate_new(udev));
  udev_enumerate_add_match_subsystem(enum_serial.get(), "usb-serial");
  udev_enumerate_add_match_parent(enum_serial.get(), iface);
  udev_enumerate_scan_devices(enum_serial.get());

  struct udev_list_entry* entry = nullptr;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enum_serial.get())) {
    // a usb-serial driver is connected to this interface
    LOG(INFO) << "Found usb-serial interface.";
    return true;
  }
  return false;
}

bool IsInterfaceStorage(udev_device* iface) {
  uint32_t interface_class;
  if (!GetUIntSysattr(iface, "bInterfaceClass", &interface_class))
    return false;
  // This matches USB drives, SD adapters, and so on.
  return interface_class == USB_CLASS_MASS_STORAGE;
}

bool IsInterfaceSafeToDetach(udev_device* iface) {
  // Normally the permission_broker prevents users from interfering with the
  // system usage of a USB device.

  // But in particular cases, a USB interface is deemed 'safe to detach' from
  // its kernel driver if the purpose of the driver is only exposing it to apps.
  // e.g. below the usb serial interfaces are only used by the chrome.serial
  // and WebSerial external API rather than in any intrinsic system use.

  // Storage devices are a special case that we allow to be shared to Guest VMs.
  // Chrome provides extra protections to avoid exposing these devices to
  // non-Guest VM components.

  return IsInterfaceUsbSerial(iface) || IsInterfaceStorage(iface);
}

bool IsDeviceAllowedSerial(udev_device* device) {
  // The Arduino vendor IDs are derived from https://raw.githubusercontent.com
  // /arduino/ArduinoCore-avr/master/boards.txt
  // /arduino/ArduinoCore-sam/master/boards.txt
  // /arduino/ArduinoCore-samd/master/boards.txt
  // using
  // grep -o -E  "vid\..*=(0x.*)" *boards.txt | sed "s/vid\..=//g" | sort -f | \
  // uniq -i
  const DevicePolicy::UsbDeviceId kAllowedIds[] = {
      {0x03eb, 0x2145},  // Arduino Uno WiFi Rev2 (ATmega4809)

      {0x093c, 0x1101},  // Intrepid Control Systems ValueCAN 4

      {0x0d28, 0x0204},  // BBC micro:bit

      {0x2341, 0},  // Arduino
      {0x1b4f, 0},  // Sparkfun
      {0x239a, 0},  // Adafruit
      {0x2a03, 0},  // doghunter.org
      {0x10c4, 0},  // Silicon Labs

      {0x2c99, 0},  // Prusa Research

      {0x2e8a, 0},  // Raspberry Pi

      {0x18d1, 0x5002},  // Google Servo V2
      {0x18d1, 0x5003},  // Google Servo V2
      {0x18d1, 0x500a},  // Google twinkie
      {0x18d1, 0x500b},  // Google Plankton
      {0x18d1, 0x500c},  // Google Plankton
      {0x18d1, 0x5014},  // Google Cr50
      {0x18d1, 0x501a},  // Google Servo micro
      {0x18d1, 0x501b},  // Google Servo V4
      {0x18d1, 0x501f},  // Google Suzyq
      {0x18d1, 0x5020},  // Google Sweetberry
      {0x18d1, 0x5027},  // Google Tigertail
      {0x18d1, 0x5036},  // Google Chocodile

      {0x1d50, 0x6140},  // QuickLogic QuickFeather evaluation board bootloader
      {0x1d50, 0x6130},  // TinyFPGA BX Bootloader old openmoko VID:PID
      {0x1209, 0x2100},  // TinyFPGA BX Bootloader new pid.codes VID:PID
      {0x1209, 0x5bf0},  // Arty FPGA board
  };
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  return UsbDeviceListContainsId(std::begin(kAllowedIds), std::end(kAllowedIds),
                                 vendor_id, product_id);
}

bool IsDeviceAllowedHID(udev_device* device) {
  const DevicePolicy::UsbDeviceId kAllowedIds[] = {
      {0x2e73, 0x0001},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0002},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0003},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0004},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0005},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0006},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0007},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0008},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0009},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0010},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0011},  // BackyardBrains Neuron SpikerBox
      {0x2e73, 0x0012},  // BackyardBrains Neuron SpikerBox
  };
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  return UsbDeviceListContainsId(std::begin(kAllowedIds), std::end(kAllowedIds),
                                 vendor_id, product_id);
}

bool IsInternallyConnectedUsbDevice(udev_device* device) {
  const DevicePolicy::UsbDeviceId kAllowedIds[] = {
      {0x0c27, 0x3bfa},  // USB card reader
  };
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  return UsbDeviceListContainsId(std::begin(kAllowedIds), std::end(kAllowedIds),
                                 vendor_id, product_id);
}

Rule::Result DenyClaimedUsbDeviceRule::ProcessUsbDevice(udev_device* device) {
  const char* device_syspath = udev_device_get_syspath(device);
  if (!device_syspath) {
    return DENY;
  }

  udev* udev = udev_device_get_udev(device);
  ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev));
  udev_enumerate_add_match_subsystem(enumerate.get(), "usb");
  udev_enumerate_scan_devices(enumerate.get());

  bool found_claimed_interface = false;
  bool found_unclaimed_interface = false;
  bool found_adb_interface = false;
  bool found_only_safe_interfaces = true;
  struct udev_list_entry* entry = nullptr;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enumerate.get())) {
    const char* entry_path = udev_list_entry_get_name(entry);
    ScopedUdevDevicePtr child(udev_device_new_from_syspath(udev, entry_path));

    // Find out if this entry's direct parent is the device in question.
    struct udev_device* parent = udev_device_get_parent(child.get());
    if (!parent) {
      continue;
    }
    const char* parent_syspath = udev_device_get_syspath(parent);
    if (!parent_syspath || strcmp(device_syspath, parent_syspath) != 0) {
      continue;
    }

    const char* child_type = udev_device_get_devtype(child.get());
    if (!child_type || strcmp(child_type, "usb_interface") != 0) {
      // If this is not a usb_interface node then something is wrong, fail safe.
      LOG(WARNING) << "Found a child '" << entry_path
                   << "' with unexpected type: "
                   << (child_type ? child_type : "(null)");
      return DENY;
    }

    const char* driver = udev_device_get_driver(child.get());
    if (driver) {
      LOG(INFO) << "Found claimed interface with driver: " << driver;
      found_claimed_interface = true;
      found_only_safe_interfaces =
          found_only_safe_interfaces && IsInterfaceSafeToDetach(child.get());
    } else {
      found_unclaimed_interface = true;
    }

    if (IsInterfaceAdb(child.get())) {
      LOG(INFO) << "Found ADB interface.";
      found_adb_interface = true;
    }
  }

  if (found_claimed_interface) {
    // Don't allow detaching the driver from fixed (internal) USB devices
    // unless it is in the allow list.
    if (GetRemovableSysattr(device) == RemovableAttr::kFixed &&
        !IsInternallyConnectedUsbDevice(device)) {
      LOG(INFO) << "Denying fixed USB device with driver.";
      return DENY;
    }

    if (found_only_safe_interfaces)
      LOG(INFO) << "Found only detachable interface(s), safe to claim.";

    if (IsDeviceDetachableByPolicy(device) || IsDeviceAllowedSerial(device) ||
        IsDeviceAllowedHID(device) || found_adb_interface ||
        found_only_safe_interfaces)
      return ALLOW_WITH_DETACH;
    else
      return found_unclaimed_interface ? ALLOW_WITH_LOCKDOWN : DENY;
  } else {
    return IGNORE;
  }
}

}  // namespace permission_broker
