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

namespace {

constexpr char kCrosUsbLocation[] = "CROS_USB_LOCATION";

enum class CrosUsbLocationProperty {
  kUnknown,
  kInternal,
  kExternal,
};

std::optional<CrosUsbLocationProperty> GetCrosUsbLocationProperty(
    udev_device* device) {
  const char* prop = udev_device_get_property_value(device, kCrosUsbLocation);
  if (!prop) {
    return std::nullopt;
  }
  std::string tag(prop);

  if (tag == "external") {
    return CrosUsbLocationProperty::kExternal;
  } else if (tag == "internal") {
    return CrosUsbLocationProperty::kInternal;
  } else {
    if (tag != "unknown") {
      VLOG(1) << "Unexpected value for CROS_USB_LOCATION property: '" << tag
              << "'";
    }
    return CrosUsbLocationProperty::kUnknown;
  }
}

bool HasInternalAncestors(udev_device* device) {
  udev_device* ancestor = udev_device_get_parent(device);
  bool internal_ancestors = false;

  while (ancestor != nullptr) {
    const char* subsystem = udev_device_get_subsystem(ancestor);
    if (strcmp(subsystem, "usb")) {
      break;
    }

    auto location = GetCrosUsbLocationProperty(ancestor);
    if (!location.has_value()) {
      continue;
    }

    if (location == CrosUsbLocationProperty::kExternal ||
        location == CrosUsbLocationProperty::kUnknown) {
      std::string loc_str = (location == CrosUsbLocationProperty::kExternal)
                                ? "external"
                                : "unknown";
    } else if (location == CrosUsbLocationProperty::kInternal) {
      // TODO(b/267951284) - should we track this, and see if we get false
      // positives?
      internal_ancestors = true;
    }
    ancestor = udev_device_get_parent(ancestor);
  }

  return internal_ancestors;
}

}  // namespace

namespace permission_broker {

// static
const struct VariationsFeature kEnablePermissiveUsbPassthrough {
  .name = "CrOSLateBootPermissiveUsbPassthrough",
  .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

AllowExternalTaggedUsbDeviceRule::AllowExternalTaggedUsbDeviceRule()
    : Rule("AllowExternalTaggedUsbDeviceRule") {}

AllowExternalTaggedUsbDeviceRule::~AllowExternalTaggedUsbDeviceRule() = default;

Rule::Result ProcessUsbDevice(udev_device* device,
                              CrosUsbLocationProperty location) {
  // Safety check, if we have an internal node in the device hierarchy we
  // should DENY this device, even if the device thinks it is external.
  if (HasInternalAncestors(device)) {
    return Rule::DENY;
  }

  if (location == CrosUsbLocationProperty::kExternal) {
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
    LOG(ERROR)
        << "Unable to get PlatformFeatures library, will default to IGNORE";
    return IGNORE;
  }
  if (!features_lib->IsEnabledBlocking(kEnablePermissiveUsbPassthrough)) {
    return IGNORE;
  }
  return ProcessUsbDevice(device, maybe_location.value());
}

}  // namespace permission_broker
