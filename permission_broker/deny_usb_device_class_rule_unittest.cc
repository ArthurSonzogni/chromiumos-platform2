// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/deny_usb_device_class_rule.h"

#include <gtest/gtest.h>
#include <linux/usb/ch9.h>

namespace permission_broker {

class DenyUsbDeviceClassRuleTest : public testing::Test {
 public:
  DenyUsbDeviceClassRuleTest() : rule_(USB_CLASS_HUB) {}
  ~DenyUsbDeviceClassRuleTest() override = default;

 protected:
  DenyUsbDeviceClassRule rule_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DenyUsbDeviceClassRuleTest);
};

TEST_F(DenyUsbDeviceClassRuleTest, IgnoreNonUsbDevice) {
  ASSERT_EQ(Rule::IGNORE, rule_.Process("/dev/loop0",
                                        Rule::ANY_INTERFACE));
}

TEST_F(DenyUsbDeviceClassRuleTest, DISABLED_DenyMatchingUsbDevice) {
  ASSERT_EQ(Rule::DENY, rule_.Process("/dev/bus/usb/001/001",
                                      Rule::ANY_INTERFACE));
}

}  // namespace permission_broker
