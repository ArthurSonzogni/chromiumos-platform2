// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/allow_external_tagged_usb_device_rule.h"

#include <base/logging.h>
#include <gtest/gtest.h>
#include <libudev.h>

#include <cstring>
#include <memory>
#include <set>
#include <string>

#include "base/memory/scoped_refptr.h"
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/login_manager/dbus-constants.h>
#include <featured/fake_platform_features.h>
#include <permission_broker/rule.h>
#include <permission_broker/rule_test.h>
#include <permission_broker/udev_scopers.h>

using testing::_;
using testing::Invoke;
using testing::Return;

using std::set;
using std::string;

namespace permission_broker {

class AllowExternalTaggedUsbDeviceRuleTest : public RuleTest {
 public:
  AllowExternalTaggedUsbDeviceRuleTest() = default;
  AllowExternalTaggedUsbDeviceRuleTest(
      const AllowExternalTaggedUsbDeviceRuleTest&) = delete;
  AllowExternalTaggedUsbDeviceRuleTest& operator=(
      const AllowExternalTaggedUsbDeviceRuleTest&) = delete;

  ~AllowExternalTaggedUsbDeviceRuleTest() override = default;

 protected:
  void SetUp() override {
    ScopedUdevPtr udev(udev_new());
    ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev.get()));
    udev_enumerate_add_match_subsystem(enumerate.get(), "usb");
    udev_enumerate_scan_devices(enumerate.get());

    struct udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry,
                            udev_enumerate_get_list_entry(enumerate.get())) {
      const char* syspath = udev_list_entry_get_name(entry);
      ScopedUdevDevicePtr device(
          udev_device_new_from_syspath(udev.get(), syspath));
      EXPECT_TRUE(device.get());

      const char* devtype = udev_device_get_devtype(device.get());
      if (!devtype || strcmp(devtype, "usb_interface") != 0) {
        continue;
      }

      ScopedUdevDevicePtr parent(udev_device_get_parent(device.get()));
      EXPECT_TRUE(parent.get());

      const char* devnode = udev_device_get_devnode(parent.get());
      if (!devnode) {
        continue;
      }

      const char* parent_removable =
          udev_device_get_sysattr_value(parent.get(), "removable");
      string path = devnode;

      if (!parent_removable) {
        unknown_devices_.insert(path);
      } else if (strcmp(parent_removable, "removable") == 0) {
        removable_devices_.insert(path);
      } else if (strcmp(parent_removable, "fixed") == 0) {
        fixed_devices_.insert(path);
      } else {
        unknown_devices_.insert(path);
      }
    }
  }

  AllowExternalTaggedUsbDeviceRule rule_;
  set<string> removable_devices_;
  set<string> fixed_devices_;
  set<string> unknown_devices_;
};

TEST_F(AllowExternalTaggedUsbDeviceRuleTest, IgnoreAllDevicesWhenDisabled) {
  auto bus = base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options{});
  auto feature_lib = std::make_unique<feature::FakePlatformFeatures>(bus);

  feature_lib->SetEnabled("EnabledPermissiveUsbPassthrough", false);

  if (unknown_devices_.empty()) {
    LOG(WARNING) << "Tests may be incomplete because there are no unknown "
                    "devices connected.";
  }
  if (fixed_devices_.empty()) {
    LOG(WARNING) << "Tests may be incomplete because there are no fixed "
                    "devices connected.";
  }
  if (removable_devices_.empty()) {
    LOG(WARNING) << "Tests may be incomplete because there are no removable "
                    "devices connected.";
  }

  for (const string& device : unknown_devices_) {
    EXPECT_EQ(Rule::IGNORE, rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
  for (const string& device : fixed_devices_) {
    EXPECT_EQ(Rule::IGNORE, rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
  for (const string& device : removable_devices_) {
    EXPECT_EQ(Rule::IGNORE, rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
}

// TODO(b/267951284) - add more tests once udev rules add correct tag

}  // namespace permission_broker
