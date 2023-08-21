// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/allow_external_tagged_usb_device_rule.h"

#include <libudev.h>

#include <cstring>
#include <optional>
#include <string>

#include "base/logging.h"
#include "featured/feature_library.h"
#include "permission_broker/rule.h"
#include "permission_broker/rule_utils.h"

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
    : Rule("AllowExternalTaggedUsbDeviceRule") {}

AllowExternalTaggedUsbDeviceRule::~AllowExternalTaggedUsbDeviceRule() = default;

Rule::Result ProcessUsbDevice(udev_device* device,
                              CrosUsbLocationProperty location) {
  // Safety check, if we have an internal node in the device hierarchy we
  // should DENY this device, even if the device thinks it is external.
  CrosUsbLocationProperty ancestors_location = AncestorsLocation(device);
  if (ancestors_location == CrosUsbLocationProperty::kInternal) {
    return Rule::DENY;
  }

  if (location == CrosUsbLocationProperty::kExternal) {
    return Rule::ALLOW_WITH_DETACH;
  } else if ((location == CrosUsbLocationProperty::kInternal ||
              location == CrosUsbLocationProperty::kUnknown) &&
             ancestors_location == CrosUsbLocationProperty::kExternal) {
    // device erroneously reported that it is not external, but has an external
    // ancestor.
    return Rule::ALLOW_WITH_DETACH;
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
          RuleUtils::kEnablePermissiveUsbPassthrough)) {
    return IGNORE;
  }
  return ProcessUsbDevice(device, maybe_location.value());
}

}  // namespace permission_broker
