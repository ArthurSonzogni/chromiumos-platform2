// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/allow_external_tagged_usb_device_rule.h"

#include <libudev.h>

#include <cstring>
#include <optional>

#include "base/logging.h"
#include "featured/feature_library.h"
#include "permission_broker/rule.h"
#include "permission_broker/rule_utils.h"
#include "permission_broker/udev_scopers.h"

namespace permission_broker {

CrosUsbLocationProperty AncestorsLocation(udev_device* device) {
  bool internal_ancestors = false;
  bool external_ancestors = false;

  for (udev_device* ancestor = udev_device_get_parent(device);
       ancestor != nullptr; ancestor = udev_device_get_parent(ancestor)) {
    const char* subsystem = udev_device_get_subsystem(ancestor);
    if (strcmp(subsystem, "usb")) {
      break;
    }

    auto location = GetCrosUsbLocationProperty(ancestor);
    if (!location.has_value()) {
      continue;
    }

    if (location == CrosUsbLocationProperty::kExternal) {
      external_ancestors = true;
    } else if (location == CrosUsbLocationProperty::kInternal) {
      // TODO(b/267951284) - should we track this, and see if we get false
      // positives?
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

AllowExternalTaggedUsbDeviceRule::AllowExternalTaggedUsbDeviceRule()
    : Rule("AllowExternalTaggedUsbDeviceRule"),
      // If unable to load form-factor, assume most conservative case.
      running_on_chromebox_(GetFormFactor() == FormFactor::kChromebox ||
                            GetFormFactor() == FormFactor::kUnknown) {}

AllowExternalTaggedUsbDeviceRule::~AllowExternalTaggedUsbDeviceRule() = default;

Rule::Result ProcessUsbDevice(udev_device* device,
                              CrosUsbLocationProperty location) {
  // Safety check, if we have an internal node in the device hierarchy we
  // should DENY this device, even if the device thinks it is external.
  CrosUsbLocationProperty ancestors_location = AncestorsLocation(device);
  if (ancestors_location == CrosUsbLocationProperty::kInternal) {
    return Rule::DENY;
  }

  // Loop through the child nodes (interfaces, really) to ascertain if any of
  // them have an attached driver - meaning they are 'claimed' by the host
  // kernel.
  udev* udev = udev_device_get_udev(device);
  ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev));
  udev_enumerate_add_match_subsystem(enumerate.get(), "usb");
  udev_enumerate_add_match_parent(enumerate.get(), device);
  udev_enumerate_scan_devices(enumerate.get());

  bool found_claimed_interface = false;
  bool found_unclaimed_interface = false;
  struct udev_list_entry* child = nullptr;
  udev_list_entry_foreach(child,
                          udev_enumerate_get_list_entry(enumerate.get())) {
    const char* entry_path = udev_list_entry_get_name(child);
    // udev_enumerate_add_match_parent includes the parent entry, skip it.
    if (!strcmp(udev_device_get_syspath(device), entry_path)) {
      continue;
    }
    ScopedUdevDevicePtr child(udev_device_new_from_syspath(udev, entry_path));

    const char* child_type = udev_device_get_devtype(child.get());
    // Safety check - child nodes of a USB device should only be interfaces.
    if (!child_type || strcmp(child_type, "usb_interface") != 0) {
      LOG(WARNING) << "Found a child interface '" << entry_path
                   << "' with unexpected type: "
                   << (child_type ? child_type : "(null)");
      return Rule::DENY;
    }

    const char* driver = udev_device_get_driver(child.get());
    if (driver) {
      found_claimed_interface = true;
    } else {
      found_unclaimed_interface = true;
    }
  }

  // The basic logic for what decision this rule will reach:
  // - If no claimed interfaces exist for the device in question, we will likely
  //   allow (pending connection to an external port).
  // - If there are claimed interfaces but no unclaimed ones, we allow the
  //   device to be used on successfully detaching kernel drivers.
  // - If there are both claimed and unclaimed interfaces, we allow the device
  //   to be used if privileges on the device are dropped.
  Rule::Result allow_variant;
  if (found_claimed_interface) {
    allow_variant = found_unclaimed_interface ? Rule::ALLOW_WITH_LOCKDOWN
                                              : Rule::ALLOW_WITH_DETACH;
  } else {
    allow_variant = Rule::ALLOW;
  }

  // The top level ALLOW/DENY decision hinges on the internal/external property
  // of the device in question, and we also want to check for devices that
  // mistakenly identify as internal when they are really not.
  if (location == CrosUsbLocationProperty::kExternal) {
    return allow_variant;
  } else if ((location == CrosUsbLocationProperty::kInternal ||
              location == CrosUsbLocationProperty::kUnknown) &&
             ancestors_location == CrosUsbLocationProperty::kExternal) {
    // device erroneously reported that it is not external, but has an external
    // ancestor.
    return allow_variant;
  } else if (location == CrosUsbLocationProperty::kInternal) {
    return Rule::DENY;
  }

  return Rule::IGNORE;
}

Rule::Result AllowExternalTaggedUsbDeviceRule::ProcessDevice(
    udev_device* device) {
  const char* device_syspath = udev_device_get_syspath(device);
  if (!device_syspath) {
    return DENY;
  }
  const char* const subsystem = udev_device_get_subsystem(device);
  if (!subsystem || strcmp(subsystem, "usb")) {
    return IGNORE;
  }
  auto maybe_location = GetCrosUsbLocationProperty(device);
  if (!maybe_location.has_value()) {
    return IGNORE;
  }

  auto features_lib = feature::PlatformFeatures::Get();
  if (!features_lib) {
    LOG(ERROR) << "Unable to get PlatformFeatures library, will not enable "
                  "permissive features";
    return IGNORE;
  }
  if (!features_lib->IsEnabledBlocking(
          RuleUtils::kEnablePermissiveUsbPassthrough) ||
      running_on_chromebox_) {
    return IGNORE;
  }
  return ProcessUsbDevice(device, maybe_location.value());
}

}  // namespace permission_broker
