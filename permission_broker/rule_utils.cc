// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/rule_utils.h"

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "chromeos-config/libcros_config/cros_config.h"
#include "permission_broker/allow_lists.h"

namespace {

std::string LoadFormFactor() {
  brillo::CrosConfig cros_config;
  std::string form_factor;

  if (!cros_config.GetString("/hardware-properties", "form-factor",
                             &form_factor)) {
    LOG(ERROR) << "Unable to ascertain form-factor from CrosConfig, this may "
                  "affect rule processing.";
  }

  return form_factor;
}

permission_broker::FormFactor StringToFormFactor(std::string form_factor) {
  if (form_factor.empty()) {
    return permission_broker::FormFactor::kUnknown;
  } else if (form_factor == "CHROMEBOX") {
    return permission_broker::FormFactor::kChromebox;
  }

  return permission_broker::FormFactor::kOther;
}

}  // namespace

namespace permission_broker {

const FormFactor& GetFormFactor() {
  static_assert(std::is_trivially_destructible<FormFactor>::value);
  static const FormFactor s = StringToFormFactor(LoadFormFactor());
  return s;
}

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

bool GetUIntSysattr(udev_device* device, const char* key, uint32_t* val) {
  CHECK(val);
  const char* str_val = udev_device_get_sysattr_value(device, key);
  return str_val && base::HexStringToUInt(str_val, val);
}

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

bool IsDeviceAllowedWebHID(udev_device* device) {
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id)) {
    return false;
  }

  return UsbDeviceListContainsId(std::begin(kWebHIDAllowedIds),
                                 std::end(kWebHIDAllowedIds), vendor_id,
                                 product_id);
}

template bool UsbDeviceListContainsId<const policy::DevicePolicy::UsbDeviceId*>(
    const policy::DevicePolicy::UsbDeviceId* first,
    const policy::DevicePolicy::UsbDeviceId* last,
    uint16_t vendor_id,
    uint16_t product_id);
template bool UsbDeviceListContainsId<
    std::vector<policy::DevicePolicy::UsbDeviceId>::iterator>(
    std::vector<policy::DevicePolicy::UsbDeviceId>::iterator first,
    std::vector<policy::DevicePolicy::UsbDeviceId>::iterator last,
    uint16_t vendor_id,
    uint16_t product_id);

const VariationsFeature RuleUtils::kEnablePermissiveUsbPassthrough = {
    .name = "CrOSLateBootPermissiveUsbPassthrough",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

}  // namespace permission_broker
