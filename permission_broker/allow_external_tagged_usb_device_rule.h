// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_ALLOW_EXTERNAL_TAGGED_USB_DEVICE_RULE_H_
#define PERMISSION_BROKER_ALLOW_EXTERNAL_TAGGED_USB_DEVICE_RULE_H_

#include "permission_broker/rule.h"

namespace permission_broker {

// AllowExternalTaggedUsbDeviceRule looks for USB devices that have been
// assigned 'external' or 'internal' values for their CROS_USB_LOCATION udev
// device property, and |ALLOW_WITH_DETACH| or |DENY|s them, respectively. All
// other value, including no value, are |IGNORE|ed.
class AllowExternalTaggedUsbDeviceRule : public Rule {
 public:
  AllowExternalTaggedUsbDeviceRule();
  AllowExternalTaggedUsbDeviceRule(const AllowExternalTaggedUsbDeviceRule&) =
      delete;
  AllowExternalTaggedUsbDeviceRule& operator=(
      const AllowExternalTaggedUsbDeviceRule&) = delete;

  ~AllowExternalTaggedUsbDeviceRule() override;

  Result ProcessDevice(udev_device* device) override;

  const char* const GetTagValue(udev_device* device);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_ALLOW_EXTERNAL_TAGGED_USB_DEVICE_RULE_H_
