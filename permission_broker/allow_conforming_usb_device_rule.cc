// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/allow_conforming_usb_device_rule.h"

#include <libudev.h>
#include <linux/usb/ch9.h>

#include <memory>
#include <optional>
#include <string>

#include "base/logging.h"
#include "featured/feature_library.h"
#include "permission_broker/allow_lists.h"
#include "permission_broker/rule.h"
#include "permission_broker/rule_utils.h"
#include "permission_broker/udev_scopers.h"
#include "permission_broker/usb_subsystem_udev_rule.h"

namespace {

const uint32_t kAdbClass = 0xff;
const uint32_t kAdbSubclass = 0x42;
const uint32_t kAdbProtocol = 0x1;

}  // namespace

namespace permission_broker {

CrosUsbLocationProperty AncestorsLocation(udev_device* device) {
  // Internal ancestor reporting is a little brittle here - for example some USB
  // hubs report their ports as internal, but what we really care about is
  // determining if a device is internal or external to the entire host, so we
  // attempt to ignore mis-reported internal statuses along the way.
  bool internal_ancestors = false;
  bool external_ancestors = false;

  for (udev_device* ancestor = udev_device_get_parent(device);
       ancestor != nullptr; ancestor = udev_device_get_parent(ancestor)) {
    const char* subsystem = udev_device_get_subsystem(ancestor);
    if (!subsystem || strcmp(subsystem, "usb") != 0) {
      break;
    }

    auto location = GetCrosUsbLocationProperty(ancestor);
    if (!location.has_value()) {
      continue;
    }

    if (location == CrosUsbLocationProperty::kExternal) {
      // A 'higher up' external node should overrule any 'lower' internal
      // report.
      external_ancestors = true;
      internal_ancestors = false;
    } else if (location == CrosUsbLocationProperty::kInternal) {
      internal_ancestors = true;
    }
  }

  if (internal_ancestors)
    return CrosUsbLocationProperty::kInternal;
  else if (external_ancestors)
    return CrosUsbLocationProperty::kExternal;
  else
    return CrosUsbLocationProperty::kUnknown;
}

// LEGACY FLOW FUNCTIONS

bool AllowConformingUsbDeviceRule::LoadPolicy() {
  usb_allow_list_.clear();

  auto policy_provider = std::make_unique<policy::PolicyProvider>();
  policy_provider->Reload();

  // No available policies.
  if (!policy_provider->device_policy_is_loaded())
    return false;

  const policy::DevicePolicy* policy = &policy_provider->GetDevicePolicy();
  return policy->GetUsbDetachableWhitelist(&usb_allow_list_);
}

bool AllowConformingUsbDeviceRule::IsDeviceDetachableByPolicy(
    udev_device* device) {
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

  bool allowed = UsbDeviceListContainsId(
      usb_allow_list_.begin(), usb_allow_list_.end(), vendor_id, product_id);

  if (allowed)
    LOG(INFO) << "Found allowable device via policy.";

  return allowed;
}

// Returns whether a USB interface represents the Android Debug Bridge.
// If so, then its parent node is an Android device with USB debugging
// enabled and we can detach its other interfaces to use it.
bool IsInterfaceAdb(udev_device* device) {
  uint32_t intf_class, intf_subclass, intf_protocol;
  if (!GetUIntSysattr(device, "bInterfaceClass", &intf_class) ||
      !GetUIntSysattr(device, "bInterfaceSubClass", &intf_subclass) ||
      !GetUIntSysattr(device, "bInterfaceProtocol", &intf_protocol))
    return false;

  return intf_class == kAdbClass && intf_subclass == kAdbSubclass &&
         intf_protocol == kAdbProtocol;
}

// Search all children of the interface in the 'usb-serial' subsystem
// this includes all the USB-serial converter and most micro-controllers
// USB bulk endpoints presenting a serial-like interface but not CDC-ACM
// devices (e.g. modems or boards pretending to be one)
bool IsInterfaceUsbSerial(udev_device* iface) {
  // udev_device_get_udev does NOT increase ref count on udev object, so we must
  // add a call to udev_ref here to avoid double-unrefs.
  ScopedUdevPtr udev(udev_ref(udev_device_get_udev(iface)));
  ScopedUdevEnumeratePtr enum_serial(udev_enumerate_new(udev.get()));
  udev_enumerate_add_match_subsystem(enum_serial.get(), "usb-serial");
  udev_enumerate_add_match_parent(enum_serial.get(), iface);
  udev_enumerate_scan_devices(enum_serial.get());

  struct udev_list_entry* entry = nullptr;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enum_serial.get())) {
    // A usb-serial driver is connected to this interface.
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
  bool allowed = interface_class == USB_CLASS_MASS_STORAGE;

  if (allowed)
    LOG(INFO) << "Found allowable storage interface.";

  return allowed;
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

bool IsDeviceAllowedHID(udev_device* device) {
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  bool allowed =
      UsbDeviceListContainsId(std::begin(kHIDAllowedIds),
                              std::end(kHIDAllowedIds), vendor_id, product_id);

  if (allowed)
    LOG(INFO) << "Found allowable HID device, safe to claim.";

  return allowed;
}

bool IsDeviceAllowedInternal(udev_device* device) {
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  bool allowed = UsbDeviceListContainsId(std::begin(kInternalAllowedIds),
                                         std::end(kInternalAllowedIds),
                                         vendor_id, product_id);

  if (allowed)
    LOG(INFO) << "Found allowable internal device, safe to claim.";

  return allowed;
}

bool IsDeviceAllowedSerial(udev_device* device) {
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  bool allowed = UsbDeviceListContainsId(std::begin(kSerialAllowedIds),
                                         std::end(kSerialAllowedIds), vendor_id,
                                         product_id);

  if (allowed)
    LOG(INFO) << "Found allowable serial device, safe to claim.";

  return allowed;
}

Rule::Result AllowConformingUsbDeviceRule::ProcessLegacyDevice(
    udev_device* device,
    std::optional<CrosUsbLocationProperty> cros_usb_location) {
  bool found_claimed_interface = false;
  bool found_unclaimed_interface = false;
  bool found_only_safe_interfaces = false;
  bool found_adb_interface = false;

  // udev_device_get_udev does NOT increase ref count on udev object, so we must
  // add a call to udev_ref here to avoid double-unrefs.
  ScopedUdevPtr udev(udev_ref(udev_device_get_udev(device)));

  ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev.get()));
  udev_enumerate_add_match_subsystem(enumerate.get(), "usb");
  udev_enumerate_add_match_parent(enumerate.get(), device);
  udev_enumerate_scan_devices(enumerate.get());

  struct udev_list_entry* entry = nullptr;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enumerate.get())) {
    const char* entry_path = udev_list_entry_get_name(entry);
    // udev_enumerate_add_match_parent includes the parent entry, skip it.
    if (!strcmp(udev_device_get_syspath(device), entry_path)) {
      continue;
    }
    ScopedUdevDevicePtr child(
        udev_device_new_from_syspath(udev.get(), entry_path));

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

  if (!found_claimed_interface) {
    // In general, USB devices should be allowed if we haven't found a reason to
    // DENY them.
    return ALLOW;
  }

  // In some cases external USB devices are marked as internal. Don't allow
  // detaching the driver for an internal USB device unless it has a removable
  // parent or is in the allow list.
  if (cros_usb_location == CrosUsbLocationProperty::kInternal &&
      !IsDeviceAllowedInternal(device) &&
      (AncestorsLocation(device) != CrosUsbLocationProperty::kExternal)) {
    LOG(WARNING) << "Denying fixed USB device with driver.";
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
}

// END LEGACY FLOW

Rule::Result AllowConformingUsbDeviceRule::ProcessTaggedDevice(
    udev_device* device, CrosUsbLocationProperty location) {
  CrosUsbLocationProperty ancestors_location = AncestorsLocation(device);

  // The top level ALLOW/DENY decision hinges on the internal/external property
  // of the device in question, and we also want to check for devices that
  // mistakenly identify as internal when they are really not.
  if (location == CrosUsbLocationProperty::kExternal) {
    LOG(INFO) << "Device was marked as external.";
    return ALLOW_WITH_DETACH;
  } else if ((location == CrosUsbLocationProperty::kInternal ||
              location == CrosUsbLocationProperty::kUnknown) &&
             ancestors_location == CrosUsbLocationProperty::kExternal) {
    // device erroneously reported that it is not external, but has an external
    // ancestor.
    LOG(INFO) << "Device was marked as internal, but climbing the hierarchy we "
                 "found an external ancestor.";
    return ALLOW_WITH_DETACH;
  } else if (location == CrosUsbLocationProperty::kInternal) {
    return Rule::DENY;
  }

  return Rule::IGNORE;
}

AllowConformingUsbDeviceRule::AllowConformingUsbDeviceRule()
    : UsbSubsystemUdevRule("AllowConformingUsbDeviceRule"),
      platform_features_(feature::PlatformFeatures::Get()),
      policy_loaded_(false),
      running_on_chromebox_(GetFormFactor() == FormFactor::kChromebox ||
                            GetFormFactor() == FormFactor::kUnknown) {}

AllowConformingUsbDeviceRule::~AllowConformingUsbDeviceRule() = default;

Rule::Result AllowConformingUsbDeviceRule::ProcessUsbDevice(
    udev_device* device) {
  const char* device_syspath = udev_device_get_syspath(device);
  if (!device_syspath) {
    VLOG(1) << "Device to be processed is lacking syspath";
    return DENY;
  }

  auto cros_usb_location = GetCrosUsbLocationProperty(device);

  if (!platform_features_) {
    LOG(ERROR) << "Unable to get PlatformFeatures library, will not enable "
                  "permissive features";
  } else if (platform_features_->IsEnabledBlocking(
                 RuleUtils::kEnablePermissiveUsbPassthrough) &&
             // There are more UI/UX implications that must be considered for
             // chromeboxes, disable for now.
             !running_on_chromebox_) {
    // If permissive USB is enabled, but we have no tag information, fall back
    // to legacy behavior.
    if (cros_usb_location.has_value()) {
      Result result = ProcessTaggedDevice(device, cros_usb_location.value());
      // If the tagged flow was truly not able to make a decision for a device,
      // allow the legacy flow to have an opinion.
      if (result != Result::IGNORE)
        return result;

      LOG(INFO) << "CROS_USB_LOCATION had a value but was not enough to "
                   "determine permissibility, falling back to legacy logic.";
    }
  }
  return ProcessLegacyDevice(device, cros_usb_location);
}

}  // namespace permission_broker
